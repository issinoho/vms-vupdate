/* vu_store.h - the credential store's contents: portal user and password sealed
 * with AES-256-GCM under a key derived from node identity (D13). No file I/O:
 * the caller reads and writes the text.
 *
 * This is obfuscation bound to the node, not secrecy from privileged users: the
 * key comes from values any process on the node can read (SCSNODE, system ID,
 * hardware name), so anyone who can read the file on that node can recover the
 * password. File protection (owner only) is what keeps other users out. What the
 * sealing adds: no plaintext on disk or in backups, a copied file is useless on
 * another node, and a modified file is detected. */
#ifndef VU_STORE_H
#define VU_STORE_H

#include <stddef.h>

#define VU_STORE_MAGIC "VUPDATE-CREDENTIALS 1"
#define VU_STORE_TEXT_MAX 2048
#define VU_USER_MAX 256
#define VU_PASSWORD_MAX 256

/* Seal user and password. out receives two lines: VU_STORE_MAGIC and hex
 * (salt, iv, tag, ciphertext). Returns 0, -1 when a value is too long, -5 when the
 * crypto library fails. */
int vu_store_seal(const char *nodekey, const char *user, const char *password, char *out,
                  size_t outsz);

/* Open text written by vu_store_seal. Returns 0; -1 when the text isn't a store
 * (bad magic or hex); -2 when it doesn't open with this node key (another node,
 * a changed SCSNODE, or a modified file); -5 crypto failure. */
int vu_store_open(const char *nodekey, const char *text, char *user, size_t usersz,
                  char *password, size_t passwordsz);

#endif
