# Supabase backend

Supabase provides the PostgreSQL database and the Edge Function used by both the ESP32 and the Vercel dashboard.

## Components

- `functions/console-api/index.ts`: API endpoint implementation.
- `migrations/001_console_schema.sql`: reference schema for `console_users` and `high_scores`.

## Deploy the Edge Function

From a machine with the Supabase CLI installed and authenticated:

```bash
supabase login
supabase link --project-ref YOUR_PROJECT_REF
supabase functions deploy console-api
```

Configure server-side function secrets in the Supabase project. The function expects `SUPABASE_URL` and `SUPABASE_SERVICE_ROLE_KEY`. Never copy the service-role key into this repository, the browser dashboard, or ESP32 firmware.

The existing project uses JWT verification on the function endpoint. The clients send the public anon/publishable key in the `apikey` and `Authorization` headers.

## Database schema

- `console_users`: `id`, unique `user_id`, `password`.
- `high_scores`: `id`, `user_id` foreign key, `game`, `high_score`, `updated_at`, unique (`user_id`, `game`).

If these tables already exist, treat the migration as documentation/reference and compare it to the live schema before running it.

## API actions

| Action | Purpose |
|---|---|
| `list_users` | Returns registered usernames for the console |
| `create_user` | Creates an allowed username and directional password |
| `login` | Verifies the player and returns their saved scores |
| `update_score` | Authenticates the player and preserves the best score for a game |
| `dashboard` | Returns usernames and their scores for the web dashboard |

Game keys: `snake`, `pong`, `space_invaders`, `pacman`, `breakout`, `tetris`.

## Security limitations

The current prototype stores passwords as plaintext and uses a very small four-direction password space. The public endpoint exposes `dashboard` and `list_users` actions to callers with the public key. There is no rate limiting or abuse monitoring. Replace this scheme with proper authentication/password hashing, enforce per-user authorization, add rate limiting, and decide deliberately which data the public dashboard should reveal. Keep tables protected by RLS and never expose the service-role key.
