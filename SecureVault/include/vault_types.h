#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  vault_types.h — shared vault entry structs
// ═══════════════════════════════════════════════════════════════════════════════
// VaultEntry  — read-only view returned by VaultManager::entryAt().
// VaultEntryRW — mutable copy used by serial protocol for add/update.
//
// Entry types (stored internally as uint8_t):
//   0 = login, 1 = card, 2 = identity, 3 = note, 4 = passkey
//
// On the wire (serial protocol JSON) and on disk (vault.db JSON) the type
// is serialized as a STRING ('login', 'card', 'identity', 'note') — NOT
// as an integer — because the Electron app + browser extension expect
// string type names. These inline helpers are the ONLY place the
// uint8_t↔string translation happens.
//
// ── Per-type field schemas (mirror the browser extension's window.html) ──
//   LOGIN    (0): site, url, user, pass, totp, notes, folder
//   CARD     (1): site, cardholder, cardNumber, exp, cvv, notes, folder
//   IDENTITY (2): site, firstName, lastName, email, phone, address, city,
//                 state, postal, country, ssn, passport, license, notes, folder
//   NOTE     (3): site, notes, folder
//   PASSKEY  (4): site (=domain), user (=userId), credentialId, publicKey,
//                 privateKey, created, notes, folder
//                 -- site/user are REUSED (domain/userId) rather than adding
//                 duplicate fields; credentialId/publicKey/privateKey/created
//                 are new. privateKey never leaves the device -- it is
//                 stored base64-encoded inside the same per-record
//                 AES-256-GCM ciphertext every other field already uses, so
//                 it gets the same at-rest protection as a login's password.
//
// Fields not relevant to an entry's type are stored as empty strings.
// The detail-screen renderer picks the relevant subset based on `type`.
#include "board_config.h"

// TOTP secrets are Base32-encoded random keys. Real-world lengths vary by
// issuer: 16 chars (80-bit, the classic Google Authenticator default),
// 26 chars (128-bit), or 32 chars (160-bit -- RFC 4226's own recommended
// strength, and the length of the official RFC 6238 test vector). The
// previous 32-byte buffer (31 usable chars + NUL) truncated/rejected that
// last, very common case. 64 bytes (63 usable chars) covers all of the
// above plus padding characters and gives headroom for longer secrets
// (e.g. 256-bit) some issuers use.
#define TOTP_SECRET_LEN 64

// ── Notes field length ─────────────────────────────────────────────────
// Single source of truth for the notes buffer size (RW struct, PSRAM
// storage, allocation and memset all use this). Was a hard-coded 160
// (159 usable chars), which silently truncated secure notes -- too short
// for a seed/pass phrase. 512 bytes (511 usable chars) fits a 24-word
// phrase with room to spare. Stays well inside the 2048-byte per-record
// JSON budget (VaultManager::MAX_RECORD_JSON_LEN) even for an identity
// entry with every field full. Existing vault.db files load unchanged
// (records are variable-length JSON; shorter old notes just fit).
#define VAULT_NOTES_LEN 512

// ── Passkey field lengths ──────────────────────────────────────────────
// credentialId: base64url, WebAuthn credential IDs are commonly 16-32
//   raw bytes -> up to ~44 chars; 96 gives headroom for larger IDs some
//   sites issue.
// publicKey: P-256 uncompressed point (0x04||X||Y = 65 raw bytes) as
//   base64url -> 87-88 chars + NUL.
// privateKey: P-256 scalar (32 raw bytes) as base64url -> 43-44 chars +
//   NUL. Never transmitted off-device in this form -- only used
//   internally by VaultManager::signWithPasskey().
#define PASSKEY_CREDENTIAL_ID_LEN 96
#define PASSKEY_PUBLIC_KEY_LEN    92
#define PASSKEY_PRIVATE_KEY_LEN   48
#define PASSKEY_CREATED_LEN       24

struct VaultEntry {
  const char* site;
  const char* user;
  const char* pass;
  const char* totp;
  uint8_t type;      // 0=login, 1=card, 2=identity, 3=note
  bool favorite;     // true = pinned to top of list
  bool deleted;      // true = in trash (hidden from ALL, shown in TRASH)

  // ── Per-type extra fields (point to VaultManager's storage) ──────────
  // LOGIN
  const char* url;
  const char* notes;
  const char* folder;
  // CARD
  const char* cardholder;
  const char* cardNumber;
  const char* exp;
  const char* cvv;
  // IDENTITY
  const char* firstName;
  const char* lastName;
  const char* email;
  const char* phone;
  const char* address;
  const char* city;
  const char* state;
  const char* postal;
  const char* country;
  const char* ssn;
  const char* passport;
  const char* license;
  // PASSKEY (site=domain, user=userId reused above)
  const char* credentialId;
  const char* publicKey;
  const char* created;
};

struct VaultEntryRW {
  char site[32];
  char user[48];
  char pass[48];
  char totp[TOTP_SECRET_LEN];
  uint8_t type;      // 0=login, 1=card, 2=identity, 3=note
  bool favorite;     // true = pinned to top
  bool deleted;      // true = in trash

  // LOGIN
  char url[64];
  char notes[VAULT_NOTES_LEN];
  char folder[24];
  // CARD
  char cardholder[32];
  char cardNumber[24];   // "0000 0000 0000 0000"
  char exp[8];           // "MM/YY"
  char cvv[5];           // up to 4 digits
  // IDENTITY
  char firstName[24];
  char lastName[24];
  char email[48];
  char phone[20];
  char address[48];
  char city[24];
  char state[24];
  char postal[12];
  char country[24];
  char ssn[16];          // "000-00-0000"
  char passport[24];
  char license[24];
  // PASSKEY (site/user fields above are reused as domain/userId).
  // privateKey is deliberately NOT here -- VaultEntryRW is the struct the
  // serial protocol uses to move entries to/from Electron, and the private
  // key must never cross that wire. It only ever exists inside
  // VaultManager's own PSRAM storage, written by addPasskeyEntry() and
  // read only by signWithPasskey() -- both internal to the firmware.
  char credentialId[PASSKEY_CREDENTIAL_ID_LEN];
  char publicKey[PASSKEY_PUBLIC_KEY_LEN];
  char created[PASSKEY_CREATED_LEN];
};

// ── Type serialization helpers (inline — usable from any .cpp) ──────────
static const char* const VAULT_TYPE_STRINGS[] = { "login", "card", "identity", "note", "passkey" };
static const int VAULT_TYPE_COUNT = 5;

inline const char* vaultTypeToStr(uint8_t t) {
  if (t < VAULT_TYPE_COUNT) return VAULT_TYPE_STRINGS[t];
  return "login";
}

inline uint8_t vaultStrToType(const char* s) {
  if (!s || !s[0]) return 0;  // missing/empty → login
  for (int i = 0; i < VAULT_TYPE_COUNT; i++) {
    if (strcmp(s, VAULT_TYPE_STRINGS[i]) == 0) return (uint8_t)i;
  }
  // Accept numeric strings "0".."4" for backward compat with vault.db
  // files written by older firmware that serialized type as integer.
  if (s[0] >= '0' && s[0] <= '4' && s[1] == '\0') return (uint8_t)(s[0] - '0');
  return 0;  // unknown → login
}

// ── Per-type field iteration helper ─────────────────────────────────────
// Used by the "type all with Tab" form-fill feature. Returns the next
// non-empty field value + its label, in canonical form-fill order for
// the given type. Call with index=0,1,2,... until it returns false.
//
// v9.19: Notes and Folder are NOT included here — they're not web-form
// fields and shouldn't be typed via Tab into forms. Use vaultFieldAtFull()
// for the detail screen display (which includes Notes and Folder).
//
// Order matches the browser extension's add/edit form layout, which is
// also the order a human would expect when tabbing through a web form
// (e.g. firstName → lastName → email → phone → address → ... for identity).
struct VaultFieldRef {
  const char* label;
  const char* value;
};

inline bool vaultFieldAt(const VaultEntry& e, int index, VaultFieldRef& out) {
  switch (e.type) {
    case 0: // LOGIN
      switch (index) {
        case 0: out = { "Username", e.user       }; return e.user       && e.user[0];
        case 1: out = { "Password", e.pass       }; return e.pass       && e.pass[0];
        case 2: out = { "URL",      e.url        }; return e.url        && e.url[0];
        case 3: out = { "TOTP",     e.totp       }; return e.totp       && e.totp[0];
        // v9.19: Notes and Folder are NOT web-form fields — removed from
        // the TYPE ALL iteration so they don't get typed via Tab into forms.
        // They're still shown on the detail screen (drawDetailFieldRow uses
        // a separate iteration that includes them), just not in TYPE ALL.
      }
      break;
    case 1: // CARD
      switch (index) {
        case 0: out = { "Cardholder", e.cardholder }; return e.cardholder && e.cardholder[0];
        case 1: out = { "Number",     e.cardNumber }; return e.cardNumber && e.cardNumber[0];
        case 2: out = { "Exp",        e.exp        }; return e.exp        && e.exp[0];
        case 3: out = { "CVV",        e.cvv        }; return e.cvv        && e.cvv[0];
        // v9.19: Notes and Folder removed from TYPE ALL for card (not web-form fields)
      }
      break;
    case 2: // IDENTITY
      switch (index) {
        case 0:  out = { "First",    e.firstName }; return e.firstName && e.firstName[0];
        case 1:  out = { "Last",     e.lastName  }; return e.lastName  && e.lastName[0];
        case 2:  out = { "Email",    e.email     }; return e.email     && e.email[0];
        case 3:  out = { "Phone",    e.phone     }; return e.phone     && e.phone[0];
        case 4:  out = { "Address",  e.address   }; return e.address   && e.address[0];
        case 5:  out = { "City",     e.city      }; return e.city      && e.city[0];
        case 6:  out = { "State",    e.state     }; return e.state     && e.state[0];
        case 7:  out = { "Postal",   e.postal    }; return e.postal    && e.postal[0];
        case 8:  out = { "Country",  e.country   }; return e.country   && e.country[0];
        case 9:  out = { "SSN",      e.ssn       }; return e.ssn       && e.ssn[0];
        case 10: out = { "Passport", e.passport  }; return e.passport  && e.passport[0];
        case 11: out = { "License",  e.license   }; return e.license   && e.license[0];
        // v9.19: Notes and Folder removed from TYPE ALL for identity
      }
      break;
    case 3: // NOTE
      switch (index) {
        case 0: out = { "Notes",  e.notes  }; return e.notes  && e.notes[0];
        // v9.19: Folder removed from TYPE ALL for note
      }
      break;
    case 4: // PASSKEY -- not typed via Tab (there's nothing to type; sign-in
            // happens through the browser's WebAuthn ceremony, not keystrokes)
      break;
  }
  return false;
}

// v9.19: Full field iteration — includes Notes and Folder.
// Used by the detail screen display (drawDetailScreenPerType) to show
// all fields including Notes and Folder. TYPE ALL uses vaultFieldAt()
// which skips Notes and Folder (they're not web-form fields).
inline bool vaultFieldAtFull(const VaultEntry& e, int index, VaultFieldRef& out) {
  switch (e.type) {
    case 0: // LOGIN
      switch (index) {
        case 0: out = { "Username", e.user       }; return e.user       && e.user[0];
        case 1: out = { "Password", e.pass       }; return e.pass       && e.pass[0];
        case 2: out = { "URL",      e.url        }; return e.url        && e.url[0];
        case 3: out = { "TOTP",     e.totp       }; return e.totp       && e.totp[0];
        case 4: out = { "Notes",    e.notes      }; return e.notes      && e.notes[0];
        case 5: out = { "Folder",   e.folder     }; return e.folder     && e.folder[0];
      }
      break;
    case 1: // CARD
      switch (index) {
        case 0: out = { "Cardholder", e.cardholder }; return e.cardholder && e.cardholder[0];
        case 1: out = { "Number",     e.cardNumber }; return e.cardNumber && e.cardNumber[0];
        case 2: out = { "Exp",        e.exp        }; return e.exp        && e.exp[0];
        case 3: out = { "CVV",        e.cvv        }; return e.cvv        && e.cvv[0];
        case 4: out = { "Notes",      e.notes      }; return e.notes      && e.notes[0];
        case 5: out = { "Folder",     e.folder     }; return e.folder     && e.folder[0];
      }
      break;
    case 2: // IDENTITY
      switch (index) {
        case 0:  out = { "First",    e.firstName }; return e.firstName && e.firstName[0];
        case 1:  out = { "Last",     e.lastName  }; return e.lastName  && e.lastName[0];
        case 2:  out = { "Email",    e.email     }; return e.email     && e.email[0];
        case 3:  out = { "Phone",    e.phone     }; return e.phone     && e.phone[0];
        case 4:  out = { "Address",  e.address   }; return e.address   && e.address[0];
        case 5:  out = { "City",     e.city      }; return e.city      && e.city[0];
        case 6:  out = { "State",    e.state     }; return e.state     && e.state[0];
        case 7:  out = { "Postal",   e.postal    }; return e.postal    && e.postal[0];
        case 8:  out = { "Country",  e.country   }; return e.country   && e.country[0];
        case 9:  out = { "SSN",      e.ssn       }; return e.ssn       && e.ssn[0];
        case 10: out = { "Passport", e.passport  }; return e.passport  && e.passport[0];
        case 11: out = { "License",  e.license   }; return e.license   && e.license[0];
        case 12: out = { "Notes",    e.notes     }; return e.notes     && e.notes[0];
        case 13: out = { "Folder",   e.folder    }; return e.folder    && e.folder[0];
      }
      break;
    case 3: // NOTE
      switch (index) {
        case 0: out = { "Notes",  e.notes  }; return e.notes  && e.notes[0];
        case 1: out = { "Folder", e.folder }; return e.folder && e.folder[0];
      }
      break;
    case 4: // PASSKEY -- detail screen view. Private key intentionally has
            // no case here: VaultEntry (the read-only view every UI/serial
            // path uses) never carries it in the first place.
      switch (index) {
        case 0: out = { "Domain",   e.site         }; return e.site         && e.site[0];
        case 1: out = { "User ID",  e.user         }; return e.user         && e.user[0];
        case 2: out = { "Cred ID",  e.credentialId }; return e.credentialId && e.credentialId[0];
        case 3: out = { "Public Key", e.publicKey  }; return e.publicKey    && e.publicKey[0];
        case 4: out = { "Created",  e.created      }; return e.created      && e.created[0];
        case 5: out = { "Notes",    e.notes        }; return e.notes        && e.notes[0];
        case 6: out = { "Folder",   e.folder       }; return e.folder       && e.folder[0];
      }
      break;
  }
  return false;
}
