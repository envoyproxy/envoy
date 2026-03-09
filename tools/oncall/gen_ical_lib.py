from dataclasses import dataclass
from datetime import date


@dataclass
class IcalEvent:
    updated: date
    start_date: date
    uid: str
    summary: str

    @property
    def lines(self) -> list[str]:
        return [
            self._dtstamp,
            self._uid,
            self._dtstart,
            self._summary,
            self.duration,
            *self.extra_lines,
        ]

    @property
    def _dtstamp(self) -> str:
        """Edit timestamp, in 20220102T123456Z format"""
        return f"DTSTAMP:{self.updated.strftime("%Y%m%dT%H%M%SZ")}"

    @property
    def _uid(self) -> str:
        """Required UID"""
        return f"UID:{self.uid}"

    @property
    def _dtstart(self) -> str:
        """Event start date in 20220102 format"""
        return f"DTSTART:{self.start_date.strftime("%Y%m%d")}"

    @property
    def _summary(self) -> str:
        return f"SUMMARY:{self.summary}"

    @property
    def duration(self) -> str:
        raise NotImplementedError("abstract class")

    @property
    def extra_lines(self) -> list[str]:
        return []

    def __str__(self) -> str:
        return "\n".join(["BEGIN:VEVENT", *self.lines, "END:VEVENT"])


@dataclass
class IcalRecurringEvent(IcalEvent):
    every_n_weeks: int
    exclude_days: list[date]

    def _intersects_day(self, d: date) -> list[date]:
        if d < self.start_date:
            # in the past
            return False
        diff_in_days = (d - self.start_date).days
        diff_in_weeks = diff_in_days // 7
        return diff_in_weeks % self.every_n_weeks == 0

    @property
    def _excluded_days(self) -> list[date]:
        return [d for d in self.exclude_days if self._intersects_day(d)]

    @property
    def _exdates(self) -> str | None:
        if self._excluded_days:
            return f"\nEXDATE:{','.join([d.strftime("%Y%m%d") for d in self._excluded_days])}"
        return None

    @property
    def _rrule(self) -> str:
        return f"RRULE:FREQ=WEEKLY;INTERVAL={self.every_n_weeks};BYDAY=SU"

    @property
    def duration(self) -> str:
        return "DURATION:P1W"

    @property
    def extra_lines(self) -> list[str]:
        ret = [self._rrule]
        exdates = self._exdates
        if exdates:
            ret.append(exdates)
        return ret


@dataclass
class IcalOverrideEvent(IcalEvent):
    duration_days: int

    @property
    def duration(self) -> str:
        return f"DURATION:P{self.duration_days}D"


def ical_file_format(events: list[IcalEvent]) -> str:
    return "\n".join([
        "BEGIN:VCALENDAR",
        "PRODID:-//Envoy//On call rotation generator script//EN", "VERSION:2.0",
        *(str(event) for event in events),
        "END:VCALENDAR\n",
    ])
