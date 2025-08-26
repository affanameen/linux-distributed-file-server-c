# Linux Distributed File Server (C, Multi-Process, Sockets)

A C-based **distributed file server** built for Advanced Systems Programming (ASP).  
Implements **multi-process servers and a client** communicating over TCP sockets on Linux.

- **S1** — front server (routes client requests, stores `.c` files).
- **S2** — backend server for `.pdf` files.
- **S3** — backend server for `.txt` files.
- **S4** — backend server for `.zip` files.
- **Client** — interactive CLI (multi-file upload/download/remove).
---

## Features

- **Upload**: send up to **3 files** in one command, automatically routed by file type.
- **Download**: retrieve up to **2 files** in one command.
- **Remove**: delete up to **2 files** in one command.
- **List**: view available files by directory, grouped by extension.
- **Tar Download**: bundle `.c`, `.pdf`, or `.txt` files into a `.tar`.

---

## Build

Requires Linux with `gcc`, `make`, and POSIX sockets.

```bash
sudo apt update
sudo apt install -y build-essential
make

---

## Repo Structure
.
├── S1.c
├── S2.c
├── S3.c
├── S4.c
├── s25client.c   # client
├── README.md
└── .gitignore