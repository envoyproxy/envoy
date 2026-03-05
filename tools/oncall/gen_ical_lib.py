from datetime import date, timedelta

from tools.oncall.parsing import rotation_from_yaml, overrides_from_yaml, Override

_HEADER = """BEGIN:VCALENDAR
PRODID:-//Envoy//On call rotation generator script//EN
VERSION:2.0"""

_FOOTER = "END:VCALENDAR\n"


def _every_n_weeks(n: int) -> str:
    return f"RRULE:FREQ=WEEKLY;INTERVAL={n};BYDAY=SU"


def _date(t: date) -> str:
    return t.strftime("%Y%m%d")


def _date_with_zero_time(t: date) -> str:
    return t.strftime("%Y%m%dT000000Z")


def _date_intersects(seats: int, start_date: date, d: date) -> bool:
    """Returns true if the date d occurs within a week specified by `start_date`
    on a rotation of `seats` weeks, e.g. if seats was 3 and start_date was Jan 1,
    would return true if d is in the week Jan 1 to Jan 7 or if d is in the week
    Jan 22 to Jan 29, and so on."""
    if d < start_date:
        # in the past
        return False
    diff_in_days = (d - start_date).days
    diff_in_weeks = diff_in_days // 7
    return diff_in_weeks % seats == 0


def _override_days(overrides: list[Override]) -> list[date]:
    dates: list[date] = []
    for override in overrides:
        d = override.start
        for _ in range(override.duration_days):
            dates.append(d)
            d += timedelta(days=1)
    return dates


def _exdates(seats: int, start_date: date, override_days: list[date]) -> str:
    excluded_days = list(
        filter(lambda d: _date_intersects(seats, start_date, d),
               override_days))
    if excluded_days:
        return f"\nEXDATE:{','.join(map(_date, excluded_days))}"
    return ""


def gen_ical() -> str:
    start_date, updated, rotation = rotation_from_yaml()
    overrides = overrides_from_yaml()
    override_days: list[date] = _override_days(overrides)
    events: list[str] = []
    seats = len(rotation)
    stamp = f"DTSTAMP:{_date_with_zero_time(updated)}"
    for oncall in rotation:
        event = f"""BEGIN:VEVENT
{stamp}
UID:{oncall}
DTSTART:{_date(start_date)}
DURATION:P1W
SUMMARY:Envoy on-call ({oncall})
{_every_n_weeks(seats)}{_exdates(seats, start_date, override_days)}
END:VEVENT"""
        events.append(event)
        start_date += timedelta(days=7)

    for override in overrides:
        assert override.oncall in rotation, f"override.oncall = {override.oncall} is not listed in rotation.yaml"
        event = f"""BEGIN:VEVENT
{stamp}
UID:{override.oncall}-override-{_date(override.start)}
DTSTART:{_date(override.start)}
DURATION:P{override.duration_days}D
SUMMARY: Envoy on-call (override -> {override.oncall})
END:VEVENT"""
        events.append(event)
    return f"""{_HEADER}
{"\n".join(events)}
{_FOOTER}"""
