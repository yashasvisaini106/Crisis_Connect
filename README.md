# CrisisConnect - Real-Time Emergency Help and Communication System

## Final Architecture
`Victim Browser -> Web Backend (Auth + API) -> Real-Time TCP Bridge -> C++ Socket Server -> Responder/Admin Clients`

## Implemented Modules

### 1. Emergency Alert Module (Victim)
- SOS button
- Emergency type selection:
  - MEDICAL
  - ACCIDENTAL
  - FIRE
  - CRIME
- Auto timestamp created by backend
- Sends emergency case to backend API and forwards priority alert to C++ TCP engine

### 2. Real-Time Chat Engine (C++)
- TCP socket server using WinSock2 (`Server/server.cpp`)
- Multi-client handling with per-client thread
- Thread safety with mutex (or Win32 fallback for current MinGW runtime)
- Priority detection for emergency keywords
- Emergency broadcast targeted to responders/admins (+ sender)
- Broadcast normal messages to all
- Emergency logs in `logs/emergency_messages.log`

### 3. Responder Dashboard
- Active emergency list
- Live chat window per case
- Case status visibility (`ACTIVE` / `RESOLVED`)
- Refresh and real-time updates via WebSocket

### 4. Admin Monitoring Panel
- View all emergencies
- Assign responders to case
- Close cases (mark resolved)

## Project Structure
```text
CrisisConnect/
|-- Server/
|   `-- server.cpp
|-- Client/
|   `-- client.cpp
|-- WebBackend/
|   |-- server.js
|   `-- package.json
|-- Frontend/
|   |-- index.html
|   |-- styles.css
|   `-- app.js
|-- logs/
|   `-- emergency_messages.log
`-- README.md
```

## Backend API Summary
- `POST /api/login` -> login and token issue
- `GET /api/bootstrap` -> initial user/session/case state
- `POST /api/emergencies` -> create emergency alert
- `GET /api/emergencies` -> list all emergencies
- `POST /api/emergencies/:id/chat` -> case chat message
- `POST /api/emergencies/:id/assign` -> admin assigns responder
- `POST /api/emergencies/:id/resolve` -> responder/admin closes case
- `GET /api/responders` -> responder usernames

## Build and Run (Windows)

### 1. Build C++ server and client
```powershell
cd .\Server
g++ -std=c++17 server.cpp -o server.exe -lws2_32

cd ..\Client
g++ -std=c++17 client.cpp -o client.exe -lws2_32
```

### 2. Start C++ TCP engine
```powershell
cd ..\Server
.\server.exe
```

### 3. Start Web Backend (Auth + API + WS + TCP Bridge + Static Frontend)
```powershell
cd ..\WebBackend
npm install
npm start
```

### 4. Open Web App
Open browser at:
- `http://localhost:4000`

### 5. Optional: Run Console TCP client
```powershell
cd ..\Client
.\client.exe
```

## Real-World Value
- Victims can trigger high-priority alerts quickly
- Responders get focused emergency broadcasts in real-time
- Admin can monitor, assign, and close incidents centrally
- Message and event timelines provide operational audit trail

## Note on Threading Toolchain
Your installed MinGW runtime lacks full `std::thread` support. The C++ code auto-falls back to Win32 threading while preserving thread-safe behavior.
