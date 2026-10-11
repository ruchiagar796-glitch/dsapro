# HTTP API

Base URL: the same origin as the site. All bodies are JSON (`Content-Type: application/json`); amounts are strings or numbers with at most 2 decimals; money in responses is an exact string such as `"1500.50"`.

**Authentication:** log in to receive the `bms_session` cookie (`HttpOnly; SameSite=Strict; Secure` on https). Every **POST/PUT** made with a session must also send header `X-CSRF-Token: <token>` (the token is returned by `/api/auth/login`, `/api/admin/login` and `/api/auth/session`).

**Response shape**
```json
{ "success": true,  "message": "Deposited Rs 100.00 successfully", "data": { ... } }
{ "success": false, "error": { "code": "INVALID_INPUT", "message": "...", "fields": { "email": "..." } } }
```
Status codes: 200/201 ok - 400 invalid input - 401 not signed in - 403 wrong role / CSRF / deactivated account - 404 not found - 409 conflict - 415 body not JSON - 422 business rule (insufficient funds, limits) - 429 too many attempts - 500 server problem (generic message).

## Public
| Method & path | Body | Result |
|---|---|---|
| `GET /healthz` | - | `ok` |
| `POST /api/auth/register` | `fullName, email, phone, password, confirmPassword` | 201 `{customerId, accountNumber, fullName, email}`; 400 with per-field `error.fields`; max 10 per IP/hour |
| `POST /api/auth/login` | `email, password` | 200 `{role, name, csrfToken, redirect}` + cookie; 401 identical message for unknown e-mail and wrong password; 429 after repeated failures (`Retry-After`) |
| `POST /api/admin/login` | `username, password` | same, `redirect: "/admin"` |
| `GET /api/auth/session` | - | `{authenticated:false}` or `{authenticated:true, role, name, csrfToken}` |
| `POST /api/auth/logout` | `{}` (+CSRF) | destroys the server session, clears the cookie |

## Customer (role = customer; the customer is ALWAYS the session's owner)
| Method & path | Notes |
|---|---|
| `GET /api/me/overview` | profile, account, totals, 5 recent transactions |
| `GET /api/me/account` | account details, limits, totals (account number also masked) |
| `GET/PUT /api/me/profile` | `PUT {fullName, phone}` - nothing else can be changed |
| `POST /api/me/password` | `{currentPassword, newPassword, confirmPassword}`; signs out other devices |
| `POST /api/me/deposit` | `{amount, idempotencyKey?}` |
| `POST /api/me/withdraw` | `{amount, idempotencyKey?}` |
| `POST /api/me/transfer` | `{toAccount, amount, idempotencyKey?}`; returns `reference` |
| `GET /api/me/recipient?account=N` | masked recipient name; rate limited (20/min) |
| `GET /api/me/transactions` | `type=all\|deposit\|withdrawal\|transfer\|reversal`, `from`, `to` (epoch seconds), `limit` (<=100), `offset` |

Transaction object: `id, reference, type, label, direction (credit|debit), amount, balanceAfter, timestamp, timestampIso, description, status (Completed|Reversed)`.
`idempotencyKey` (8-64 chars `[A-Za-z0-9_-]`): repeating the same key + same request returns the first result (`"replayed": true`) instead of moving money again; the same key with a different request gives 409.

## Admin (role = admin)
| Method & path | Notes |
|---|---|
| `GET /api/admin/overview` | totals, recent transactions, `storage {known, persistent}` |
| `GET /api/admin/customers` | `search`, `status`, `sort=created\|name\|balance`, `order` |
| `GET /api/admin/customers/{customerId}` | profile (never credentials), account, totals, recent transactions |
| `GET /api/admin/accounts` | `search`, `status`, `sort=number\|name\|balance`, `order` |
| `GET /api/admin/accounts/{accountNumber}` | account detail |
| `POST /api/admin/accounts/{n}/status` | `{status: "active"\|"deactivated"}` |
| `GET /api/admin/transactions` | like customer filters plus `search`, `account` |
| `GET/POST /api/admin/reversal` | peek / perform reversal of the latest reversible transaction |
| `GET/POST /api/admin/queue`, `POST /api/admin/queue/serve` | service tokens (FIFO) |
| `POST /api/admin/password` | change own password (min 10 characters) |

Pages (HTML): `/`, `/login`, `/register`, `/admin-login`, `/dashboard` (customer session required), `/admin` (admin session required); others return the 404 page.
