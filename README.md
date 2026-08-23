# Ninlil

Ninlil is a small, portable C11 runtime for durable messaging over intermittent, low-bandwidth links.

This repository is the canonical Ninlil repository as of 2026-08-23.

## Current status

The repository is being bootstrapped from the reviewed direct-radio and persistent-security-state software candidate. No production release has been declared. ESP32-S3/SX1262 physical RF and hard-power acceptance remain mandatory gates.

## Project principles

- never report an uncertain result as success;
- commit durable state before external effects;
- keep queues, tables, payloads, retries, and waits bounded;
- keep product-specific policy outside the reusable runtime;
- keep the first-party project, tests, and documentation within a 50,000-line budget.

The initial source import is developed on a review branch before it is merged into `main`.
