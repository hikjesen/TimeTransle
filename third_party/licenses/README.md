# Third-party license staging

The repository records reviewed dependency versions and license identifiers without duplicating all upstream license files in source control.

`scripts/package.ps1` collects exact license and notice texts from the revision-pinned native dependency source trees and the installed npm production tree. A distributable package is rejected if any expected text is missing. The generated package contains `THIRD-PARTY-NOTICES.txt` plus `licenses/native/` and `licenses/npm/`.
