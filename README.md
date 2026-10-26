# Console Connect 🎮

**A personal hardware-to-full-stack learning project — documented as a project record, not a production release.**

Console Connect is an ESP32 handheld gaming console project. It brings together embedded firmware, a small player/high-score API, a Supabase PostgreSQL database, and a web dashboard. The repository records the architecture, implementation, experiments, and lessons from connecting these pieces.

The goal was not just to build a console: it was also to learn how a frontend, backend, and database fit together with a physical device.

## Project at a glance

- **Device:** ESP32 DevKit, SSD1306 128×64 OLED, analog joystick
- **Firmware:** Arduino/C++
- **Backend:** Supabase Edge Function
- **Database:** Supabase PostgreSQL
- **Dashboard:** Static HTML/CSS/JavaScript; deployed separately on Vercel
- **Device features:** Player selection, directional-password login, and online high-score integration
- **Games implemented in firmware:** Snake, Pong, Space Invaders, Pac-Man, and Breakout
- **Work in progress:** Tetris appears in the menu but is not implemented as a playable game yet

## Architecture

```text
 ESP32 handheld console ── HTTPS/JSON ──┐
                                        ├── Supabase Edge Function
 Web dashboard ─────────── HTTPS/JSON ──┘          │
                                                   ▼
                                          Supabase PostgreSQL
                                          console_users
                                          high_scores
```

Both clients use the `console-api` function. See [the architecture notes](docs/architecture.md) for the request flow and trust boundaries.

## Repository guide

- [`firmware/esp32/`](firmware/esp32/) — Arduino sketch, wiring, and device setup notes.
- [`supabase/functions/console-api/`](supabase/functions/console-api/) — documented API implementation.
- [`supabase/migrations/`](supabase/migrations/) — reference database schema.
- [`web-dashboard/`](web-dashboard/) — dashboard source and notes.
- [`docs/architecture.md`](docs/architecture.md) — architecture and data flow.

## Project record and limitations

This repository is primarily a **portfolio and learning record**. It is not a claim that the system is production-ready or that every component can be deployed directly from this repository without adjustment.

- The firmware sketch is the main device artifact.
- The dashboard source in this repository is a documented snapshot and may not be byte-for-byte identical to the separately hosted dashboard.
- The Edge Function source was reconstructed for documentation from the known API contract; it has **not been verified as an exact export of the deployed function**.
- The SQL migration is a reference schema. Compare it with the live database before running it.
- The firmware, backend, and dashboard have not all been compiled and integration-tested together as part of this repository review.
- The current prototype uses four-direction passwords stored as plaintext and exposes user-list/dashboard data through public-key-accessible API actions. This is not suitable for sensitive accounts or production authentication.

These caveats are included so the repository represents the work honestly. The hosted service and the files in this repository should be treated as related project artifacts, not automatically as identical builds.

## Exploring the project

Start with the firmware README for hardware and Arduino IDE notes, then read the architecture document to follow how the device communicates with the backend.

This repository is intended to make the project understandable to someone reviewing the work, learning from it, or discussing it as part of a portfolio. It is not intended to serve as a one-click production deployment guide.

## Security note

Never commit Wi-Fi passwords, access tokens, database passwords, or Supabase service-role/secret keys. Keep personal credentials out of public source control. The public anon/publishable key is client configuration and must never be confused with a service-role key.
