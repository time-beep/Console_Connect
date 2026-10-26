# Architecture and Data Flow

This document records the intended structure of Console Connect. It describes the design, not a claim that every checked-in file is an exact copy of the live deployment.

```text
 ESP32 game console ── HTTPS + JSON ──┐
                                      ├── Supabase Edge Function: console-api
 Web dashboard ─────── HTTPS + JSON ──┘                  │
                                                        ▼
                                              Supabase PostgreSQL
                                              console_users
                                              high_scores
```

## Request flow

1. The ESP32 connects to Wi-Fi and sends JSON requests to the `console-api` endpoint.
2. The browser dashboard requests player and score data from the same endpoint.
3. The Edge Function reads or updates database rows through a server-side database client.
4. The function returns JSON to the calling client.

## Data model recorded in this repository

- `console_users`: player identity and the prototype's directional password.
- `high_scores`: per-player, per-game best score and update timestamp.

The SQL migration is a reference for this structure. Compare it with the live Supabase schema before applying it.

## Source and deployment caveat

The Edge Function source in this repository was reconstructed from the known API contract and has not been verified as an exact export of the deployed function. The dashboard is also a source snapshot. Do not assume that redeploying these files would reproduce the hosted system without comparison and testing.

## Trust boundaries and lessons

- The public anon/publishable key is client configuration; the service-role key is privileged and belongs only in server-side secrets.
- The prototype's four-direction passwords are stored as plaintext and have a very small search space.
- User-list and dashboard actions are accessible through the public API.
- Robust authentication, per-user authorization, rate limiting, and deliberate control over public dashboard data would be needed before any production use.

The main learning value of this architecture is understanding the data path between embedded firmware, an HTTP API, a database, and a browser-based frontend.
