# Crypto T-Key S3 — User Intent and Feature Inventory

This is a review checklist built from the user-authored prompts found in Claude session records dated September 24–25, 2026. It separates those prompts from assistant summaries and from claims in the repository. Select one choice for each row; **Unsure** means the scope or security model needs discussion.

The feature descriptions below paraphrase the user's requests. The quoted snippets are short excerpts to help locate the original intent. “Documented” describes what the current README or security audit says; it does not independently verify that the feature works in the current, locally modified firmware.

## The project in the user's own words

The user wants a personal **Crypto T-Key S3** built around a LilyGo T-Dongle S3: a security key that can be used for online accounts and supported system access, plus a crypto wallet with a setup page and a distinctive, customizable device interface. The user wants account setup to be easy, crypto assets and addresses to be dependable, wallet data to be recoverable, and the device to remain useful as a portable authenticator. In the user's words, they want it to be “my security key for everything, all my accounts, all system access, and everything. that is supported.”

The project also has a cross-cutting operating request: use the AI Vault and coordinate with peer AIs. The newer policy for AIs reviewing each other's durable self-changes is captured separately in the [cross-agent learning proposal](scratchpad/learning_proposal_cross_agent_self_change_review.md); it is not a firmware feature.

## Selectable feature list

For each row, select **one**: `[ ] KEEP` · `[ ] REMOVE` · `[ ] UNSURE`.

### Security key and account setup

- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Make the T-Key a FIDO security key/passkey for every account and system that supports it, including Google, Claude, GitHub, browsers, and supported host authentication. The user said “literally any account that supports a security key.”
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Provide an easy account-enrollment flow from the setup page (for example, links to account security pages that guide registration). Confirm the feasible flow; accounts generally control their own credential registration, and the device cannot silently enroll itself.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Add phone-based/Bluetooth authentication so an iPhone can use the key without a direct port connection. The user raised this as a question; confirm platform support and whether Bluetooth is a requirement.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Explore phone fingerprint approval for remote authentication or approval actions. The prompt says “if there’s a way,” so keep this conditional pending a concrete threat model and supported platform design.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Fix the reported computer USB connection/recognition instability while preserving operation on other devices.

### Wallet, assets, and recovery

- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Support the user's chosen set of dependable, widely used crypto assets, including reputable stablecoins and established meme coins. The user emphasized “legit long-standing” projects; decide and document an allowlist rather than adding coins solely from rankings.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Show useful coin identity in the interface, including coin symbols/icons rather than only a generic color fill.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Show current portfolio value and timely price/balance changes. The user asked to see a buy or sale reflected “within seconds if not, live action completely”; define realistic data freshness and network/power requirements.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Add Bitcoin receive-address QR support so an address need not be typed. **Confirm direction:** the captured prompt says “use a QR code instead of having to type in the address directly”; it does not settle whether the QR is displayed by the T-Key for receiving, scanned by it for sending, or both. The README currently documents displayed receive QR codes for selected chains.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Make supported wallet families, address derivation, and receive/send flows reliable enough that assets sent to a displayed address reach the intended wallet. The user explicitly raised concern about funds not appearing or being lost.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Investigate a Lightning address/payment path to avoid waiting for on-chain Bitcoin confirmation, and explain its custody/connectivity trade-offs before choosing an implementation.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Set a clear and safe storage/portfolio limit and explain where wallet value actually resides; the user asked whether device memory could be overloaded by large holdings.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Provide secure seed backup and full restore for a worst-case recovery, including after a duress/reset scenario. Define recovery and duress behavior before implementing it.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Include saved PINs in removable-media backup. This was requested later, but decide whether copying PINs is safe or whether backup should instead preserve encrypted recovery material without making PINs exportable.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Encrypt SD backups so only the T-Key firmware can use them, protect backup actions with the setup-page password, support restore, and provide an explicit full-card wipe. Explain that flash-media overwrite cannot guarantee physical erasure.

### Setup page, networks, and device appearance

- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Replace the setup-page placeholder image with a LilyGo T-Dongle S3 shown with this project's firmware/branding.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Keep the setup-page password experience the user liked, with clear security behavior.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Keep Wi-Fi scanning and allow many saved access points; automatically reconnect to an available saved network, including at a friend's home.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Store Wi-Fi profiles on the SD card. The user explicitly said they were unsure about this; decide only after reviewing the security and recovery implications.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Make regular and duress PIN lengths consistent with the length selected for the regular PIN (up to eight digits), or choose another clearly explained rule that avoids mismatched lengths.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Offer at least ten preset colors plus a custom color picker that lets the user drag to the intended color and preview it before committing.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Offer approximately five to ten LED effects, including user-defined combinations of colors and effects if practical.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Synchronize rainbow UI color transitions with the rainbow LED effect.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Make each supported cryptocurrency's ambient lighting and UI treatment distinct and recognizable, including meaningful color/pattern changes when activity is detected.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Add time and date to the home screen, alongside selected useful portfolio information.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Replace the generic “Ready” screen with a personalized home screen: device name, selectable visuals, and practical display choices.

### Engineering and project maintenance

- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Analyze the complete local and GitHub firmware project, perform a bug/security review, and maintain documentation and branding. The user asked for a “complete firmware update and bug check across the entire system” and separately requested a security audit and documentation/branding upgrade.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Add a comprehensive regression check for every firmware change, based on the user's report that the color picker did not preview/commit and several effects appeared to fall back to rainbow.
- [ ] KEEP · [ ] REMOVE · [ ] UNSURE — Keep a Bitcoin/crypto wallet feature under consideration, but do not treat the earlier “Should I do it yet or not? Or could you monitor it?” as authorization to add an unreviewed asset or start monitoring it.

## Evidence and current documented state

### User-authored session records consulted

- **P01 — Asset/monitoring question:** Claude session `4c7664ab-cc4a-4793-a8d9-a897308df728`, 2026-09-24 21:14:27 UTC: “I want to test a new cryptocurrency like Bitcoin to my TK wallet. Should I do it yet or not? Or could you monitor it?”
- **P02 — Security key account coverage:** Claude session `f36d1164-fb18-4a98-b971-8451df365ef2`, 2026-09-24 00:45:37 UTC: wants the FIDO key working and registered with online accounts supporting security keys.
- **P03 — Wallet/firmware/QR/phone request:** same Claude session, 2026-09-24 05:25:18 UTC: asks for a complete firmware update and bug check, QR instead of typing a Bitcoin address, analysis of the local and GitHub project, and asks conditionally about remote phone authentication/fingerprint approval.
- **P04 — Naming/docs/security:** same Claude session, 2026-09-24 07:06:51 UTC: asks for the repo name Crypto-T-key-S3 plus documentation, branding, and security audit/upgrade. The prompt record also contains assistant-written follow-up text; only the opening request is treated as the user's intent here.
- **P05 — Asset quality:** same Claude session, 2026-09-24 14:57:07 UTC: wants established, widely used coins, including the meme-coin side and stablecoins.
- **P06 — Full setup, UI, wallet, recovery, phone/USB request:** Claude session `4c7664ab-cc4a-4793-a8d9-a897308df728`, 2026-09-25 04:00:56 UTC. This long prompt contains the setup-image, PIN, saved Wi-Fi, colors/effects, color-picker interaction, rainbow synchronization, coin icon, crypto-specific lighting, time/date/price, account setup, storage limits, Lightning, seed/SD recovery, personalized home screen, computer USB behavior, and conditional Bluetooth requests summarized above.
- **P07 — General security-key scope:** Claude session `f36d1164-fb18-4a98-b971-8451df365ef2`, 2026-09-25 05:01:05 UTC: “I want this to be my security key for everything, all my accounts, all system access, and everything. that is supported.”

The project’s Codex and Antigravity histories also contain agent-generated handoffs and summaries. Those were not treated as direct user requests. The above session records are the traceable direct-prompt evidence used for this draft.

### Repository documentation (claims, not fresh verification)

- The [README](README.md) describes CTAP2/U2F and passkeys, a PIN-gated crypto wallet, receive QR codes for Bitcoin/EVM/Solana/Dogecoin, an authenticated Wi-Fi setup page, and LED/color configuration.
- The [security audit](docs/SECURITY-AUDIT.md) calls the project experimental and not yet safe as a sole authenticator or cold wallet. It lists open flash-secret exposure and other release gaps. This matters when deciding how much wallet value and which credentials to entrust to the device.
- These docs were read to distinguish documented features and risks from the user's requested outcome. No firmware behavior was tested as part of this inventory.

## Cross-agent operating request (separate from the device feature list)

The user wants the AI Vault treated as a shared durable knowledge base and wants agents to inspect the other agents' durable changes (skills, plugins, memory, integrations, and configuration) and independently review/adapt useful changes in each agent's own style. This needs a reviewable system-wide policy and native per-agent adapters. Its proposal is available at [learning proposal](scratchpad/learning_proposal_cross_agent_self_change_review.md). No shared AI configuration or Vault file has been changed in this task.

## Review

Mark KEEP, REMOVE, or UNSURE for each feature. For every UNSURE item, add the decision or question you want answered. QR direction, phone/Bluetooth authentication, account-enrollment automation, backup treatment of PINs, Wi-Fi profile storage, and acceptable wallet/portfolio limits particularly need a precise scope.
