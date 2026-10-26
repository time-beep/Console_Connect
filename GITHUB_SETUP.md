# Publish this project to GitHub

The source tree is ready to publish, but a GitHub repository must first exist under your account. Once you create an empty repository named `console-connect`, run these commands from this folder:

```bash
git init
git add .
git commit -m "Initial Console Connect project structure"
git branch -M main
git remote add origin https://github.com/YOUR_GITHUB_USERNAME/console-connect.git
git push -u origin main
```

Before pushing, review the files and confirm that only placeholders are present for Wi-Fi credentials and the Supabase public key. Never commit a service-role/secret key.

To connect Vercel to GitHub, import the repository in Vercel and set the project's **Root Directory** to `web-dashboard`. The dashboard is a static HTML site and does not require a build command.
