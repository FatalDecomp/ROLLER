# ROLLER

ROLLER recreates and extends FATAL RACING while preserving its original gameplay
concepts and behavior.

## Language

**Race Session**: A single on-track experience, whether it is a competitive race
or a winner or champion showcase. It excludes lobby coordination, loading, and
preparation. _Avoid_: Race lifecycle; unqualified race when ambiguity matters

**Race Session Kind**: The fixed classification assigned when a Race Session
begins: competitive, winner showcase, or champion showcase. _Avoid_: Mode, race
type

**Active Human Competitor**: A human-controlled competitor who has neither
finished nor been permanently eliminated and can still affect a competitive Race
Session.

**Competitive Outcome Settlement**: The point at which no Active Human
Competitors remain and the competitive outcome is settled. AI competitors do not
delay this point. _Avoid_: Race completion, first finisher, all competitors
finished

**Pre-Start**: The Race Session period after competitors are placed on track but
before driving control is released. The race countdown occurs during this
period.

**Running**: The Race Session period beginning when driving control is released
and continuing while the competitive outcome remains open.

**Outcome-Settled**: The Race Session period after its competitive outcome is
fixed but while on-track activity may continue. _Avoid_: Complete, post-race
