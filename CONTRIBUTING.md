# Contributing

Ninlil favors small, reviewable changes over broad rewrites.

## Development flow

- Create a focused branch from `main`.
- Keep one pull request to one coherent responsibility.
- Update the relevant specification or decision record when behavior changes.
- Add observable-behavior tests, including failure and restart cases.
- Run `./scripts/ci.sh` before requesting review.
- Keep generated files and build outputs out of commits.

## Pull request requirements

A pull request must state:

- the problem and the intended invariant;
- what is deliberately out of scope;
- exact test commands and results;
- hardware or external gates that were not run;
- compatibility and persistent-format effects;
- first-party line-count impact.

Large changes should be split. A first-party diff over roughly 1,000 lines needs a clear reason and usually should become multiple pull requests.

## Commit messages

Use concise imperative subjects. Examples:

```text
fix: reject ambiguous flash commit markers
feat: add bounded radio receive queue
docs: define secure-session restart contract
```

## Third-party code

Pin the exact upstream version or commit, record its license and provenance, and keep local patches explicit. Do not silently edit vendored sources to satisfy first-party style rules.
