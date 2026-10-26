# Architecture

```text
┌──────────────────┐       HTTPS + JSON       ┌────────────────────────┐
│ ESP32 game console├────────────────────────>│ Supabase Edge Function │
└──────────────────┘                          │      console-api       │
                                              └────────────┬───────────┘
┌──────────────────┐       HTTPS + JSON                    │ server-side DB client
│ Vercel dashboard ├───────────────────────────────────────┘
└──────────────────┘                                       ▼
                                              ┌────────────────────────┐
                                              │ Supabase PostgreSQL    │
                                              │ console_users          │
                                              │ high_scores            │
                                              └────────────────────────┘
```

## Request flow

1. The ESP32 connects to Wi-Fi and sends HTTPS requests to `console-api`.
2. The dashboard requests dashboard data from the same Edge Function.
3. The Edge Function uses a server-side database client to read or update rows.
4. The function returns JSON to the calling client.

The public project URL and anon/publishable key can be present in client code. The service-role key bypasses normal RLS protections and must remain in server-side secrets only.

## Current trust boundaries

The function is a shared API for both clients. Changes to request names, payload fields, game keys, or response formats can break the firmware and dashboard, so update and test all clients together.

The current prototype uses four-direction passwords stored as plaintext. This is not suitable for sensitive accounts. Improve authentication, authorization, rate limiting, and dashboard data exposure before treating the service as production-ready.
