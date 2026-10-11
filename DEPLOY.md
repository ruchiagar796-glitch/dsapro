# Deploying BankPro on Render

Render builds the `Dockerfile` for you (it compiles the C++ code and **runs all tests during the build** - a failing test stops the deployment instead of putting a broken version online).

## 1. Update your existing service

Your repository and the Render service already exist, so you only push and redeploy:
```bash
git add -A && git commit -m "Customer + admin portals" && git push
```
Render redeploys automatically if auto-deploy is on (otherwise: Dashboard -> your service -> **Manual Deploy -> Deploy latest commit**).

### Environment variables (Dashboard -> your service -> Environment)
| Variable | Value | Notes |
|---|---|---|
| `BMS_ADMIN_USERNAME` | `admin` (or your choice) | first administrator's user name |
| `BMS_ADMIN_PASSWORD` | **a strong password you choose** | **secret**; min 10 characters with a letter and a digit. Never put it in the code or on GitHub |
| `BMS_TRUST_PROXY` | `1` | lets the server see the real client address and the https flag behind Render's proxy |
| `BMS_DATA_DIR` | `/data` | already the Docker default; only change it if your disk is mounted elsewhere |
| ~~`BMS_USER`, `BMS_PASSWORD`~~ | **delete them** | the old site-wide password no longer exists; the app ignores these |

`PORT` is set by Render itself; the server listens on it automatically. `render.yaml` contains the same settings; because your service already exists, editing the variables in the dashboard is the surest way.

## 2. First administrator
On start-up, if `BMS_ADMIN_PASSWORD` is set and no admin called `BMS_ADMIN_USERNAME` exists, that admin is created (password stored only as an Argon2id hash). An existing admin is **never overwritten** - to change the password later, sign in to the Admin portal -> *My Password*. On the free plan (temporary storage) the admin is simply re-created from the environment variables at every start. There is no default or public password anywhere in the code.

## 3. Persistent storage (IMPORTANT)
**The free plan has a temporary file system.** Everything in `/data` - customers, balances, transactions - is erased whenever the service restarts, sleeps and wakes, or redeploys. The Admin dashboard shows an orange warning in that case, and the startup log says `storage : TEMPORARY`.

To keep data you need a **paid web service plan with a persistent disk** (Render disks are not available on the free plan; check Render's current pricing):
1. Dashboard -> your service -> **Settings -> Instance type**: choose a paid plan (e.g. *Starter*).
2. **Disks -> Add Disk**: Name `bank-data`, **Mount Path `/data`**, Size 1 GB.
3. Redeploy. The startup log should now say `storage : persistent disk`.

(`render.yaml` has the same settings commented out - uncomment them if you manage the service through the Blueprint.) A service with a disk runs **one** instance and has a brief downtime on each deploy; that is normal and also why a single data folder is safe.

`docker-entrypoint.sh` fixes the ownership of the freshly mounted disk and then runs the server as an unprivileged user. This step is written for Render's root-owned disk mount but has not been tested on Render itself - if the log shows `Cannot open ... for writing`, tell me the exact message.

If you cannot pay for a disk, the app still works as a demo: just remember that data resets, and use the same admin environment variables every time.

## 4. Checking that it works
* `https://<your-service>.onrender.com/healthz` should answer `ok`.
* The home page shows *Customer Banking* and *Admin Login*.
* Register a customer, sign in, deposit; sign in at `/admin-login` with your admin credentials.
* Render **Logs** show start-up information (never passwords).

## 5. Troubleshooting
| Symptom | Fix |
|---|---|
| Build fails in the test step | open the build log; the failing test name tells you what broke. Nothing goes live |
| Admin login always fails | `BMS_ADMIN_PASSWORD` missing / too short (<10 chars or no digit) - the log says `rejected`. Fix the variable and redeploy |
| "Too many attempts" | brute-force protection; wait 15 minutes |
| Logged out unexpectedly | sessions live in memory and end on restart or after 30 minutes idle / 8 hours |
| Data vanished | you are on temporary storage - see section 3 |
