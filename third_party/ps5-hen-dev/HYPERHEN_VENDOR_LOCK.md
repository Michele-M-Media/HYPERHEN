# HyperHEN vendor lock: ps5-hen legacy engine

Source imported from the user-supplied `ps5-hen-dev-latest.zip` archive.

- Upstream snapshot: `699870d16ca4cd5cd70fc88f8cf61b3953d4a292`
- Exact firmware profiles: 1.00, 1.01, 1.02, 1.05, 1.10, 1.11, 1.12, 1.13,
  1.14, 2.00, 2.20, 2.25, 2.26, 2.30, 2.50, 2.70, 3.00, 3.10, 3.20, 3.21,
  4.00, 4.02, 4.03, 4.50, and 4.51.
- HyperHEN compiles this source internally and selects it only for those exact
  profiles. HyperCore and the newer daemon route are deliberately not launched
  on this branch; the common embedded HyperHEN Automount stage starts after the
  legacy completion marker.

Local changes write HyperHEN readiness markers and rebrand runtime log prefixes;
firmware logic and profile data stay upstream-owned.
