# Web Dashboard — Project Record

This folder contains a static HTML/CSS/JavaScript dashboard snapshot for viewing Console Connect player profiles and saved high scores.

It is included to document the web portion of the project and how it was intended to communicate with the Supabase Edge Function. It is **not a guarantee that this copy is identical to the separately hosted dashboard or ready to deploy unchanged**.

## What it demonstrates

- A browser-based dashboard for player records and game high scores
- A client request to the shared `console-api` endpoint
- Rendering API data in a searchable player view
- A simple static frontend without a build step

## Configuration notes

The source contains placeholder values for the API URL and public anon/publishable key. Those placeholders are intentional in this repository snapshot. Do not add private credentials or a Supabase service-role key to browser code.

## Deployment status

The existing hosted dashboard is managed separately. This repository is being maintained as a project record, so there is no Git-connected deployment workflow documented here. The presence of `vercel.json` records the static hosting configuration, but does not mean the GitHub repository is connected to Vercel.

## Important limitation

The dashboard calls an API whose source in this repository has not been verified as an exact copy of the deployed Edge Function. Treat this folder as a documented implementation snapshot, not a reproducible production release.
