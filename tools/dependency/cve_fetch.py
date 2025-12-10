import argparse
import asyncio
import json
import pathlib
import sys
from calendar import monthrange
from datetime import datetime

import aiohttp

from aio.run import runner

# TODO(phlax): Move this to toolshed so its properly tested/linted

BASE_URL = "https://services.nvd.nist.gov/rest/json/cves/2.0"
PAGE_SIZE = 2000
MAX_CONCURRENCY = 4
MAX_RETRIES = 10
BACKOFF_BASE = 10


class NvdDownloaderException(Exception):
    pass


def date_month(value: str) -> datetime:
    """Parse YYYY-MM format into a datetime (first day of the month)."""
    try:
        return datetime.strptime(value, "%Y-%m")
    except ValueError:
        raise argparse.ArgumentTypeError(
            f"Invalid date format: '{value}'. Expected YYYY-MM (e.g. 2025-03).")


def date_months(value) -> set[datetime]:
    """Parse a comma-separated list of months in %Y-%m format."""
    months = value.split(",")
    for m in months:
        date_month(m)
    return set(months)


class NvdDownloader(runner.Runner):

    @property
    def end_date(self):
        return datetime(self.args.end.year, self.args.end.month + 1, 1)

    @property
    def output_path(self) -> pathlib.Path | None:
        path = self.args.output_path
        if not path:
            return None
        path = path.resolve()
        path.mkdir(exist_ok=True, parents=True)
        return path

    @property
    def overwrite(self):
        return self.args.overwrite

    @property
    def semaphore(self):
        return asyncio.Semaphore(self.args.max_concurrency)

    @property
    def skip(self) -> set:
        return self.args.skip or set()

    @property
    def start_date(self):
        return self.args.start

    def add_arguments(self, parser) -> None:
        super().add_arguments(parser)
        parser.add_argument("start", type=date_month, help="Start month (publication).")
        parser.add_argument("end", type=date_month, help="End month (publication).")
        parser.add_argument(
            "--skip", type=date_months, help="Months to skip. Comma separated in `%Y-%m` format.")
        parser.add_argument(
            "--overwrite", action="store_true", help="Overwrite files if the exist.")
        parser.add_argument(
            "--output_path", type=pathlib.Path, help="Path to save downloaded JSON data.")
        parser.add_argument(
            "--max_concurrency", help="Maximum concurrent requests.", default=MAX_CONCURRENCY)

    def nvd_url(self, start_date, end_date, start_index):
        return (
            f"{BASE_URL}?noRejected"
            f"&pubStartDate={start_date.strftime('%Y-%m-%dT%H:%M:%S.000')}"
            f"&pubEndDate={end_date.strftime('%Y-%m-%dT%H:%M:%S.999')}"
            f"&startIndex={start_index}")

    async def fetch_page(self, session, start_date, end_date, start_index):
        """Fetch a single page of CVEs for a given date range and start index."""
        url = self.nvd_url(start_date, end_date, start_index)
        self.log.debug(f"FETCH ({start_date.strftime('%Y-%m')}): {start_index}\n  {url}")
        for attempt in range(1, MAX_RETRIES + 1):
            async with self.semaphore:
                async with session.get(url) as resp:
                    if resp.status == 429:  # rate limited
                        wait_time = BACKOFF_BASE * attempt
                        self.log.warning(
                            f"429 Too Many Requests ({start_date.strftime('%Y-%m')}/{start_index}): attempt {attempt}/{MAX_RETRIES}, "
                            f"backing off {wait_time}s...")
                        await asyncio.sleep(wait_time)
                        continue  # retry

                    resp.raise_for_status()
                    return start_date, end_date, start_index, await resp.json()

    async def fetch_range(self, session, start_date, end_date):
        """Fetch all pages for a given 120-day (or smaller) date range."""
        start_index = 0
        tasks = []
        start_date, end_date, start_index, first_page = await self.fetch_page(
            session, start_date, end_date, start_index)
        total = first_page.get("totalResults", 0)

        yield start_date, end_date, start_index, first_page

        for start_index in range(PAGE_SIZE, total, PAGE_SIZE):
            tasks.append(
                asyncio.create_task(self.fetch_page(session, start_date, end_date, start_index)))

        for task in asyncio.as_completed(tasks):
            yield await task

    async def fetch_window(self, start_date, end_date):
        """Fetch CVEs for an arbitrary larger window, chunked into 120-day ranges."""
        async with aiohttp.ClientSession() as session:
            current = start_date
            while current < end_date:
                if current.strftime("%Y-%m") in self.skip:
                    # Move to the first day of the next month
                    if current.month == 12:
                        current = datetime(current.year + 1, 1, 1)
                    else:
                        current = datetime(current.year, current.month + 1, 1)
                    continue
                # Compute the last day of the current month
                last_day = monthrange(current.year, current.month)[1]
                chunk_end = datetime(current.year, current.month, last_day)

                # Make sure we don't go past the overall end_date
                if chunk_end > end_date:
                    chunk_end = end_date

                async for data in self.fetch_range(session, current, chunk_end):
                    yield data

                # Move to the first day of the next month
                if current.month == 12:
                    current = datetime(current.year + 1, 1, 1)
                else:
                    current = datetime(current.year, current.month + 1, 1)

    @runner.cleansup
    @runner.catches(NvdDownloaderException)
    async def run(self):
        tempdir = pathlib.Path(self.tempdir.name)
        months = {}
        async for start_date, end_date, offset, chunk in self.fetch_window(self.start_date,
                                                                           self.end_date):
            self.log.info(
                f"RECV ({start_date.strftime('%Y-%m')}): {len(chunk.get('vulnerabilities', []))} at {offset}"
            )
            if not self.output_path:
                print(json.dumps(chunk))
                continue
            month = start_date.strftime("%Y-%m")
            path = tempdir / f"{month}-{offset}.json"
            months.setdefault(month, []).append(path)
            with path.open("a", encoding="utf-8") as f:
                json.dump(chunk, f)

        if not self.output_path:
            return

        for month, files in months.items():
            files.sort(key=lambda p: int(p.stem.rsplit("-", 1)[1]))
            output_file = self.output_path / f"{month}.json"
            if output_file.exists():
                if not self.overwrite:
                    raise NvdDownloaderException(
                        f"File {output_file} exists and overwrite was not specfied")
                output_file.unlink()

            with output_file.open("a", encoding="utf-8") as outfile:
                for f in files:
                    self.log.debug(f"WRITE ({month}): {f}")
                    with f.open("r", encoding="utf-8") as infile:
                        outfile.write(infile.read())


def main(*args):
    return NvdDownloader(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
