# IRADE ORCID authentication

IRADE login page: `https://frelog.p-e.kr/irade/login`.

Register an ORCID Public API client with application URL `https://frelog.p-e.kr/irade/` and the exact **Redirect URI**:

```text
https://frelog.p-e.kr/api/irade/auth/orcid/callback
```

## Private credentials

The current server uses `/home/steam/.config/irade/orcid.env` (owner `steam`, mode `0600`; parent directory `0700`). This file is outside Git. Set only the two credential fields, without quotes:

```dotenv
ORCID_OAUTH_ENABLED=false
ORCID_CLIENT_ID=
ORCID_CLIENT_SECRET=
ORCID_ISSUER=https://orcid.org
ORCID_REDIRECT_URI=https://frelog.p-e.kr/api/irade/auth/orcid/callback
```

`ORCID_ENV_FILE` in the backend `.env` selects that file. Compose `env_file` uses `format: raw`, preserving `$`, `#` and other secret characters without interpolation. The Spring `environment` block must not override those values. Docker Compose 2.30+ is required for raw env files. Never print `docker compose config` or container environment dumps: use `config --quiet`.

After entering credentials:

```bash
python3 scripts/activate-irade-orcid.py --check
python3 scripts/activate-irade-orcid.py
```

The helper validates without executing the file, atomically enables OAuth while preserving ownership/mode, and recreates only Spring. Existing C++ and VM connections are retained. Verify Spring health and `GET /api/irade/auth/orcid/status`. No rebuild is needed to change credentials. Register sandbox credentials separately and set `ORCID_ISSUER=https://sandbox.orcid.org` only for testing; sandbox and production identifiers do not share an account identity.

## Database and identity

Apply `project-agora-DB/mssql/migrations/2026-11-13-irade-orcid.sql` followed by `2026-11-13-orcid-account-constraints.sql` with the administrator migration procedure before deploying this Spring version. The second migration updates the legacy authentication and email CHECK constraints to accept verified ORCID-only accounts; adding columns alone is insufficient. The same additive migration is included in initial database setup. Back up and verify the database first.

`users.orcid` and `users.orcid_issuer` have a filtered composite unique index. New researchers have no invented email or local password. Their ORCID identity maps to a normal Agora `user_id`, nickname/tag, role and active status. Existing Agora accounts must authenticate and explicitly start an ORCID linking flow within 10 minutes of that authentication. Matching names/emails never link accounts, and an ORCID already belonging to another account cannot be reassigned. Account erasure removes both identity fields and invalidates old tokens.

ORCID authentication uses the official server's authorization-code flow and `/authenticate` scope. Spring exchanges the code on the server and validates the returned identifier/check digit and scope. ORCID access tokens are not used for other profile operations and are not persisted or returned to the browser. Client secrets stay on the server.

## HTTP API

| Endpoint | Behavior |
| --- | --- |
| `GET /api/irade/auth/orcid/status` | Whether the provider is configured; never returns credentials |
| `GET /api/irade/auth/orcid/start?workspace=42&app=irade` | Begin login/automatic registration; bind state to HttpOnly browser cookie |
| `POST /api/irade/auth/orcid/link` | Authenticated explicit link to an existing account; JSON `{ "workspace": 42 }` optional |
| `GET /api/irade/auth/orcid/callback` | Consume expiring DB state once, verify ORCID and create a one-use login handoff |
| `POST /api/irade/auth/session` | Same-origin, cookie-bound one-use handoff; return login/MFA challenge and ORCID identity |
| `GET /api/irade/auth/me` | Authenticated researcher profile |
| `GET /api/irade/workspaces?q=proof` | Server-side workspace search with existing visibility and participation checks |
| `POST /api/irade/workspaces` | Create a private workspace; JSON `{ "name": "Research", "description": "..." }` |
| `GET /api/irade/workspaces/{id}` | Read an accessible workspace |
| `POST /api/irade/workspaces/{id}/access` | Existing canvas password/access checks and authenticated C++/Wall channel grant |

The browser login session is separate from the Agora email login UI. Its application JWT is kept in the current tab's `sessionStorage`. Existing-account password/MFA confirmation is used only for linking. MFA enrollment on an ORCID account is enforced before issuing a usable application token.

OAuth state is stored as hashes in `auth_orcid_attempts`, bound to a separate random browser cookie, expires after 10 minutes and is consumed before the token exchange. A callback replay therefore fails even after an upstream failure. A cookie-bound handoff expires after 60 seconds, is bound to the request IP and can be consumed once from the configured origin. Handoffs are in Spring memory; a restart or multiple replicas without sticky routing requires restarting login. Neither JWTs nor provider access tokens appear in redirects. Edge and inner nginx omit callback query strings from access logs and use `Referrer-Policy: no-referrer`.

## Synchronization

```text
ORCID → Spring → users.user_id → IRADE workspace access grant
  → Wall WSS → C++ workspace_files → VM /workspace CRDT cells
```

C++ continues to resolve the verified nickname/tag to the active SQL user ID and validates current workspace read/write groups. It does not accept a client-supplied ORCID as authentication. This reuses the existing synchronization and permission-revocation protocol without introducing another data store or trusting the frontend.

## Validation

ORCID client/identity/integration tests cover code exchange serialization, missing scope, ID check digits, private-email registration, repeat login, explicit linking, state/browser mismatch, expired state, replay, handoff origin/one-use checks, suspended/erased identities, private creation, search/access delegation and existing JWT/MFA/erasure regressions. UI tests use a test-only provider fixture and two real editor replicas with the isolated guest CRDT worker; they do not claim an actual ORCID user consent flow. Production user authentication must be checked with a real ORCID account after configuring credentials.

References: [ORCID authentication](https://info.orcid.org/documentation/integration-and-api-faq/), [Public API client registration](https://info.orcid.org/ufaqs/how-do-i-register-a-public-api-client/), [sign-in guidelines](https://info.orcid.org/documentation/integration-guide/orcid-oauth-sign-in-guidelines/).

## Project Agora login

The Agora `/login` page also offers ORCID when `/api/auth/providers` reports `orcid:true`. It starts the same authorization flow with `app=agora` and uses the existing registered ORCID callback. The callback returns to `/login?orcid=success`, `/login?orcid=cancelled` or `/login?orcid=failed` on the configured public origin. Default `app=irade` retains IRADE's existing callback behavior.

The return application is encoded in the random OAuth state before the complete state is hashed into the attempt record. Changing or removing the application prefix invalidates that record lookup and cannot issue a login handoff. Only the fixed Agora and IRADE paths are used; arbitrary return URLs are rejected. No schema change or new OAuth credentials are needed. Both frontends consume the same one-use session response, including MFA challenges, and share the same SQL identity and canvas permissions.

## Registration constraint regression

The first ORCID rollout added identity columns but did not update `CK_users_Auth` and `CK_users_EmailRequired`. ORCID authentication could succeed while the subsequent user INSERT failed; the rolled-back transaction left no account and the callback displayed the common failure message. Apply the account-constraints migration to fix both rules. The correction is validated against real SQL Server CHECK constraints, not only Hibernate-generated H2 tables. Start a fresh login after a failed attempt because OAuth state and authorization codes are one-use.
