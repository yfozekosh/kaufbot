-- PostgreSQL schema for kaufbot
-- Run this on your Supabase/PostgreSQL database

-- Files table
CREATE TABLE IF NOT EXISTS files (
    id                 BIGSERIAL PRIMARY KEY,
    original_file_name TEXT    NOT NULL,
    file_size_bytes    BIGINT  NOT NULL,
    saved_file_name    TEXT    NOT NULL UNIQUE,
    file_hash          TEXT    NOT NULL UNIQUE,
    is_ocr_processed   BOOLEAN NOT NULL DEFAULT FALSE,
    ocr_file_name      TEXT    NOT NULL DEFAULT '',
    created_at         TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at         TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Parsed receipts table
CREATE TABLE IF NOT EXISTS parsed_receipts (
    id           BIGSERIAL PRIMARY KEY,
    file_id      BIGINT NOT NULL UNIQUE REFERENCES files(id) ON DELETE CASCADE,
    parsed_json  JSONB  NOT NULL,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Indexes for performance
CREATE INDEX IF NOT EXISTS idx_files_hash ON files(file_hash);
CREATE INDEX IF NOT EXISTS idx_parsed_file_id ON parsed_receipts(file_id);
CREATE INDEX IF NOT EXISTS idx_files_created_at ON files(created_at DESC);

-- Optional: Create a bucket in Supabase Storage
-- Go to Supabase Dashboard > Storage > Create bucket
-- Bucket name: receipts (or configure via SUPABASE_BUCKET env var)
-- Set to private unless you want public access
