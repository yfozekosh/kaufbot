# Deployment

## Docker

```mermaid
graph TB
    subgraph Host
        ENV[".env file"]
        DATA["/data volume"]
    end

    subgraph Container
        BIN["tgbot static binary"]
    end

    subgraph External
        TG[Telegram API]
        GM[Gemini API]
        SB["Supabase (optional)"]
        PG["PostgreSQL (optional)"]
    end

    ENV -->|mount read-only| BIN
    DATA -->|mount r/w| BIN
    BIN <-->|polling| TG
    BIN -->|OCR| GM
    BIN -.->|supabase| SB
    BIN -.->|postgres| PG
```

## Docker Compose

```yaml
services:
  kaufbot:
    build: .
    restart: unless-stopped
    env_file: .env
    volumes:
      - ./data:/data
```

## Bare Metal

```mermaid
graph LR
    subgraph Server
        BIN["tgbot binary"]
        DB[("bot.db")]
        FS["/data/files/"]
    end

    BIN -->|sqlite| DB
    BIN -->|local| FS
    BIN <--> TG[Telegram API]
    BIN --> GM[Gemini API]
```

## Environment Matrix

| Backend | Storage | Database | Data Path |
|---------|---------|----------|-----------|
| local + sqlite | `/data/files/` | `/data/bot.db` | single volume |
| local + postgres | `/data/files/` | external PG | split storage |
| supabase + sqlite | Supabase bucket | `/data/bot.db` | split storage |
| supabase + postgres | Supabase bucket | external PG | fully external |
