# Rough-In Plumbing Lab

A browser-based plumbing simulator that teaches residential rough-in. You lay out cold, hot, drain and vent piping in a two-story house cutaway, turn the water on, and a built-in inspector checks your work against the 2021 International Residential Code (IRC).

Everything is in one file, `index.html`. Open it in any modern browser; there is no build step.

## What it simulates

- **Supply pressure.** 0.433 psi lost per foot of rise, Hazen-Williams friction for type L copper (C = 140), elbows and tees as equivalent pipe length, and meter loss. Fixture flow drops when pressure falls below the fixture's minimum.
- **Hot water.** The heater is fed from the cold side, so heavy hot-water demand also loads the cold main.
- **Leaks.** An uncapped pipe end sprays when the water is on and pulls pressure down for the rest of the house.
- **Drain, waste and vent.** Traps, trap-arm length to the vent (IRC Table P3105.1), slope per foot (P3005.3) and fixture-unit capacity (P3005.4.1). It also catches S-traps, crown vents, drains that shrink downstream, vents that stop inside the building, and horizontal vents below the flood rim (P3104.5).
- **Code checks.** Main and water-heater shutoffs (P2903.9), the 80 psi static limit and PRV (P2903.3.1), and thermal expansion control (P2903.4).
- **Materials cost.** A running cost for pipe, fittings and devices, used by the budget lessons.

## Lessons

1. Water in: the meter, the main shutoff and the first supply line
2. Pressure: height, friction and upsizing a trunk
3. Hot water: hooking up the heater
4. Traps & vents
5. Slope & size
6. Full rough-in: the final exam
7. High pressure: PRV and expansion tank on a 110 psi street
8. Service call: find and fix hidden faults
9. Vent puzzle: vent around a window using trap-arm sizing
10. Hilltop: 32 psi on a materials budget
11. Two-bath exam: stacked baths, velocity limits and a budget

There is also a Sandbox for free building, with adjustable street pressure and a sample house.

## Tests

```
node plumbinglab/tests/lessons.test.js
```

The test loads the simulation engine straight out of `index.html` and checks two things for every lesson: the starting layout doesn't already pass, and a reference solution meets every goal.

The simulator uses teaching values. Local code and the inspector have the final word.
