"""Secret-masking patterns shared by every script in this project.

There is exactly one definition of what a secret looks like, and it is used
BOTH to mask and to verify the masking afterwards. If those two ever diverge
the verification becomes theatre, so they share this list.

Two scripts import this:

  export-transcript.sh   masks a rendered session log
  read-banner.sh         masks the board's boot output as it is displayed

Duplicating the patterns into the second script was the obvious move and the
wrong one -- the whole reason this file exists is that the SSID leaked twice,
the second time because one copy of the rules was less complete than the other.

A pattern is (label, regex). The regex must capture the key in group 1 and the
value in group 2, because masking replaces the match with group(1) + '***'.
The (?!\\*) guard stops an already-masked value from matching again, which is
what lets the residual check mean something.
"""

import hashlib
import re

PATTERNS = [
    # WHAT IS ACTUALLY SENSITIVE IN THIS PROJECT.
    #
    # Ported from the sibling ESP32 gateway, whose patterns were all WiFi PSKs
    # and Meshtastic channel keys. None of those apply here: this board has no
    # WiFi and stores no credentials. What it does handle is LOCATION, which is
    # the thing worth masking when a transcript leaves the machine.
    #


    # Raw NMEA position from the receiver: ddmm.mmmm,N,dddmm.mmmm,E

    # A bare decimal-degree pair anywhere else -- code, comments, prose.
    ('coordinates',
     r'()(-?\d{1,2}\.\d{5,}\s*,\s*-?1?\d{1,2}\.\d{5,})'),

    # Email address.
    ('email',
     r'()([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})'),

    # A street address, in the form an order confirmation or invoice uses.
    ('street-address',
     r'(?i)()(\d{2,5}(?:\s+[A-Z][A-Za-z]*){1,4}\s+(?:CIR|ST|AVE|RD|DR|LN|BLVD|CT|WAY)\b[^\n]*)'),

    # Generic credential shapes, in case anything ever grows one.
    ('token',
     r'(?i)(\b(?:api[_-]?key|token|secret|password|passwd)\s*[=:]\s*)(?!\*)(\S{8,})'),
    ('private-key',
     r'()(-----BEGIN [A-Z ]*PRIVATE KEY-----)'),
]


def mask(body, extra=()):
    """Mask every pattern, then every literal in `extra`.

    Returns (masked_body, {label: count}). re.M is needed because the
    firmware config-dump patterns are anchored per line.
    """
    counts = {}
    for label, pat in PATTERNS:
        body, n = re.subn(pat, lambda m: m.group(1) + '***', body, flags=re.M)
        if n:
            counts[label] = n
    for literal in extra:
        n = body.count(literal)
        if n:
            body = body.replace(literal, '***')
            counts[_label(literal)] = n
    return body, counts


def _label(literal):
    """A stable name for a literal that does not disclose any of it.

    This used to be f'literal {literal[:6]}…'. That prints the first six
    characters of a secret into the very report that claims to have masked it,
    which on 2026-08-21 disclosed most of a ten-digit password. A truncated
    hash names the literal just as well and reveals nothing.
    """
    digest = hashlib.sha256(literal.encode()).hexdigest()[:8]
    return f'literal sha:{digest} ({len(literal)} chars)'


def residual(body):
    """Anything still matching after mask() is a real leak, not a false
    positive, because every pattern requires a value to follow the key."""
    out = {}
    for label, pat in PATTERNS:
        hits = re.findall(pat, body, flags=re.M)
        if hits:
            out[label] = len(hits)
    return out


def load_literals(path):
    """Read a secrets file: one literal per line, '#' comments, blanks ignored.

    Literals catch what no pattern can -- a bare SSID with no "ssid=" in front
    of it, in prose or in a string literal. Missing file is not an error.
    """
    try:
        with open(path, encoding='utf-8') as fh:
            return [ln.strip() for ln in fh
                    if ln.strip() and not ln.lstrip().startswith('#')]
    except FileNotFoundError:
        return []
