# Console Connect 🎮

**An ESP32 handheld gaming console connected to player accounts, cloud-saved high scores, and a web dashboard.**

This began as a practical learning project: build a console, then learn how a frontend, backend, and database work together by making the hardware use them.

## What it does

- Runs classic games on an ESP32 with an SSD1306 OLED and joystick.
- Supports multiple player profiles and directional-password login.
- Saves per-player high scores in Supabase PostgreSQL.
- Uses one Supabase Edge Function API for both the console and dashboard.
- Displays player records in a static web dashboard hosted on Vercel.

Games in the current firmware/API: Snake, Pong, Space Invaders, Pac-Man, Breakout, and Tetris.

## Architecture

```text
┌─────────────────────┐      HTTPS/JSON       ┌─────────────────────────┐
│ ESP32 gaming console├──────────────────────>│ Supabase Edge Function  │
└─────────────────────┘                       │       console-api       │
                                              └────────────┬────────────┘
┌─────────────────────┐      HTTPS/JSON                    │
│ Vercel web dashboard├────────────────────────────────────┘
└─────────────────────┘                                    ▼
                                              ┌─────────────────────────┐
                                              │ Supabase PostgreSQL     │
                                              │ console_users           │
                                              │ high_scores             │
                                              └─────────────────────────┘
```

The ESP32 and dashboard call the same API. The Edge Function uses a server-side service-role key to query the database. That privileged key must never be placed in browser code, firmware, or Git history.

## Repository structure

- `firmware/esp32/` — Arduino sketch, hardware pinout, and setup notes.
- `supabase/functions/console-api/` — Edge Function source.
- `supabase/migrations/` — reference SQL schema.
- `web-dashboard/` — static dashboard and Vercel config.
- `docs/architecture.md` — system architecture and data flow.

Each component folder has its own README.

## Quick start

1. **Firmware:** Open `firmware/esp32/ConsoleConnect.ino` in Arduino IDE. Follow [firmware setup](firmware/esp32/README.md) and set your own Wi-Fi credentials and public Supabase key.
2. **Supabase:** Follow [backend setup](supabase/README.md). Configure `SUPABASE_URL` and `SUPABASE_SERVICE_ROLE_KEY` as server-side function secrets; deploy `console-api`.
3. **Database:** Review [the schema reference](supabase/migrations/001_console_schema.sql). The live project may already have these tables; compare before running the SQL.
4. **Dashboard:** Follow [dashboard setup](web-dashboard/README.md). Configure the Supabase project URL and public anon/publishable key in `web-dashboard/index.html`, then deploy that folder to Vercel.

## Security limitations

This is a learning prototype, not production-grade authentication. The current implementation stores four-direction passwords as plaintext, uses a very small password space, and exposes dashboard/user-list actions through the public API. Before real-world use, migrate to proper authentication and password hashing, add rate limiting and authorization, and restrict dashboard data exposure.

Never commit Wi-Fi passwords, access tokens, database passwords, or Supabase service-role/secret keys. The public anon/publishable key is client configuration; it is not a substitute for the service-role key.

## API actions

- `list_users`
- `create_user`
- `login`
- `update_score`
- `dashboard`

## Links

- [Architecture](docs/architecture.md)
- [Firmware setup](firmware/esp32/README.md)
- [Supabase backend](supabase/README.md)
- [Web dashboard](web-dashboard/README.md)
