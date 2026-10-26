# Web dashboard

A static HTML dashboard for viewing Console Connect player profiles and saved high scores. The dashboard source is `index.html` and can be deployed directly to Vercel without a build step.

## Configure

Set `API_URL` to your Supabase Edge Function URL and `SUPABASE_ANON_KEY` to the project's public anon/publishable key. These are public client-side values. Never put a Supabase service-role/secret key in this file.

## Deploy to Vercel

1. Import this repository into Vercel and set `web-dashboard` as its Root Directory.
2. Use the static/no-framework configuration; no build command is needed.
3. Deploy and open the resulting URL.

From this folder, the Vercel CLI commands are:

```bash
vercel
vercel --prod
```

## How it works

The browser sends a POST request with `{ "action": "dashboard" }` to `console-api`. The function reads users and scores from PostgreSQL and returns JSON. The page renders a searchable grid of users and their six game scores. The dashboard does not query the database directly and does not need the service-role key.
