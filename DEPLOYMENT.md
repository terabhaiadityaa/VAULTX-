# NOIR — ATM Simulation Platform Deployment Guide

This guide describes how to deploy the NOIR ATM Simulation Platform online.

---

## 🌐 Instant Live Public Deployment

The application is currently live and operational online:

- **Live Public URL:** [https://a148950243db44.app.lhr.life](https://a148950243db44.app.lhr.life)
- **Local URL:** `http://localhost:8080`
- **Health Check Endpoint:** `https://a148950243db44.app.lhr.life/api/health`

---

## 🚀 Cloud Deployment Options

### Option 1: Render.com (1-Click Blueprint)

1. Push your repository to GitHub.
2. Sign in to [Render Dashboard](https://dashboard.render.com).
3. Click **New +** -> **Blueprint**.
4. Connect this GitHub repository. Render automatically reads `render.yaml`.
5. It will build the multi-stage `Dockerfile`, attach a 1GB persistent disk at `/data`, and provide you with a permanent `https://your-app.onrender.com` URL.

### Option 2: Fly.io (Container with Volume)

1. Install Fly CLI: `brew install flyctl` or `curl -L https://fly.io/install.sh | sh`
2. Run:
   ```bash
   fly launch
   fly volumes create atm_data --size 1
   fly deploy
   ```
3. Fly automatically builds using `Dockerfile` and deploys to global edge nodes with your persistent volume.

### Option 3: Docker & Docker Compose (Self-Hosted / VPS)

Run the container locally or on any cloud VPS (DigitalOcean, AWS EC2, Linode):

```bash
# Build and run container with persistent storage
docker-compose up --build -d

# Verify container is running
docker ps
curl http://localhost:8080/api/health
```

---

## ⚙️ Environment Variables

| Variable | Default | Purpose |
|---|---|---|
| `HOST` | `0.0.0.0` | IP interface to bind HTTP server |
| `PORT` | `8080` | Port for incoming HTTP requests |
| `DATABASE_PATH` | `database/atm.db` | SQLite database file location (`/data/atm.db` in Docker) |
| `FRONTEND_DIR` | `./frontend` | Path to static frontend files |
