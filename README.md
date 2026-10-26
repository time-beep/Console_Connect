# Console Connect 🎮

An ESP32-based handheld gaming console with classic games, player profiles, cloud-saved high scores, and a web dashboard.

## Architecture

ESP32 console and Vercel dashboard communicate with a Supabase Edge Function (`console-api`), which accesses PostgreSQL.

## Components
- `firmware/esp32/` — ESP32 firmware
- `supabase/functions/console-api/` — backend Edge Function
- `supabase/migrations/` — database schema reference
- `web-dashboard/` — Vercel dashboard

See each folder's README for setup instructions. Never commit Wi-Fi credentials or Supabase service-role keys. The current prototype's directional passwords are weak and stored as plaintext; do not use this authentication design for sensitive accounts.