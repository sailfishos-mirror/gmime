# MimeKit porting

When porting code from MimeKit, preserve applicable inline comments that
explain non-obvious behavior, edge cases, compatibility workarounds, or
algorithm invariants. Adapt them to the C/GObject implementation rather
than copying explanations of managed-only mechanisms or behavior that
the native implementation does not have. See `PORTING.md` for the porting
contract and documented native differences.
