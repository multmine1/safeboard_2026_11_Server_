# Private infrastructure portal

Static navigation for existing services. Nginx provides TLS and a separate
login; each destination keeps its own authentication.

The platform supplies `links.json`, TLS certificates, the password verifier
and generated Helm values. Instance addresses and passwords are excluded
from this repository.

Forgejo Actions runs `node --test`, scans source and the image, publishes and
signs the image, then deploys the Helm bundle with werf. The pinned base image
is supplied through `BASE_IMAGE`; delivery values come from `DEPLOY_VALUES`.

werf packages the existing Helm chart; Docker Buildx builds the image. The only
generated file allowed by the werf policy is `.ci-values.json`. Navigation and
login data use projected volumes. Nginx reloads the mounted certificate for new
TLS connections when its native certificate cache expires (60 seconds).
