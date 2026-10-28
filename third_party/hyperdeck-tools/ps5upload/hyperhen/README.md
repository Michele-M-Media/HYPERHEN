# HyperDeck build adapter

The files in `generated/` are deterministic outputs of the upstream 7.1.5
console UI build. HyperHEN stages them after `make clean`, so release builds do
not require npm or network access. The original client sources remain the
canonical UI source.

`HYPERHEN_NO_MEDIA_TILE=1` keeps the original native service and console UI,
while preventing its standalone Media tile from being registered. HyperDeck is
the sole Media entry and launches the service on demand.
