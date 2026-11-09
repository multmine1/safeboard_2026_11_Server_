# CI и доставка

OpenTofu и Ansible выполняются на Mac. CI использует отдельную VM, которую
оператор включает и выключает через Cockpit. Runner выполняет один job
одновременно. Остановка CI не останавливает уже развёрнутые приложения.
Публичное Git-зеркало Actions не запускает.

Используется официальный Forgejo Runner. Версии, образы и контрольные суммы
инструментов объявлены в `k0s/requirements/components.json`; обычный запуск
их не обновляет.

## Подготовка CI

Должны работать внутренний Forgejo, Registry и их зависимости. Включите CI VM
и подготовьте гостевую ОС, доверие CA и узел k0s командами основного README.
После генерации конфигураций создайте scoped API token и identity runner:

```console
cd ansible
ansible-playbook playbooks/ci-credentials.yml --ask-vault-pass
cd ..
python scripts/runner.py
```

Playbook использует существующего администратора Forgejo и сохраняет token,
ID/UUID runner и его конфигурацию в общем Vault. Перед повторным использованием
он проверяет token и регистрацию через API. Отсутствующие регистрации
восстанавливаются; состояние offline не вызывает повторную регистрацию.
Совпадение только числового ID при другом UUID не считается той же identity.
CLI устанавливаются на CI VM
и подключаются в Pod только для чтения. Отдельные registration token и файл
`.runner` не используются.

После установки CLI подготовьте отсутствующую signing identity:

```console
cd ansible
ansible-playbook playbooks/ci-signing.yml --ask-vault-pass
cd ..
python scripts/applications.py ci
```

Cosign создаёт зашифрованный ключ в приватном временном каталоге CI VM.
Playbook сохраняет ключ и пароль в общем Vault, затем удаляет временные файлы.
Существующая signing identity сохраняется. Runner получает её через Secret
`ci-signing`; публичные настройки находятся в ConfigMap `ci-config`.

Runner использует host executor внутри своего Pod и Docker socket выделенной
CI VM. Docker bridge и управление iptables отключены; Buildx использует host
network. Трафик проходит через LAN NIC, роутеры и их фильтры.

Docker socket даёт заданиям полномочия администратора CI VM. Runner предназначен
для доверенных внутренних проектов. Его ServiceAccount ограничен public
namespace; доступ ClusterRole для доставки не выдаётся.

## Приватный портал

Исходники находятся в `web/admin-portal`. Отдельный пароль хранится в
`vault_store.admin_portal`; аутентификация остальных сервисов сохраняется.
URL берутся из плана доступа, реальные адреса в Git не записываются.
Портал доступен только через SSH SOCKS5.

Подготовка credentials, сертификата и ресурсов платформы:

```console
cd ansible
ansible-playbook playbooks/portal-credentials.yml --ask-vault-pass
ansible-playbook playbooks/pki.yml --ask-vault-pass
cd ..
python scripts/applications.py admin-portal ci
```

Следующая команда создаёт приватный репозиторий приложения, задаёт Actions
variables/secrets и выполняет commit/push только исходников портала и его workflow:

```console
cd ansible
ansible-playbook playbooks/ci-repository.yml --ask-vault-pass
```

Репозиторий инфраструктуры эта команда не публикует. Локальная копия приложения
хранится в ignored `environment/.tools/git`; credentials Git передаются только
через окружение процесса и ASKPASS, без token в URL.

Если серверный репозиторий отсутствует, а локальная Git-история сохранилась,
playbook создаёт пустой remote и выполняет обычный `git push`. Повторная
инициализация README не создаёт отдельную историю; force-push не используется.

## Workflow

Генератор формирует `configs/generated/k0s/applications/ci/delivery.yml`.
Этапы: checkout, Node tests, Gitleaks и Trivy исходников, Docker Buildx/push,
Trivy образа по digest, Cosign sign/verify, werf bundle publish/apply.
Checkout action и базовый образ закреплены. `IMAGE_REF` содержит неизменяемый
digest; доставку выполняет отдельный release приложения.

Переменные репозитория: `BASE_IMAGE`, `IMAGE_REPOSITORY`, `BUNDLE_REPOSITORY`,
`APP_RELEASE`, `APP_NAMESPACE`, `DEPLOY_VALUES`, `DEPLOY_REVISION`.
`DEPLOY_VALUES` содержит готовые Helm values из C++, `DEPLOY_REVISION` — ревизию
подготовленного плана. Chart не вычисляет адреса или размещение. Secrets:
`REGISTRY_USERNAME`, `REGISTRY_PASSWORD`, `COSIGN_PASSWORD`.

Тег image и bundle содержит SHA приложения и ревизию C++ плана. При изменении
только variables/secrets playbook отправляет native `workflow_dispatch`, если
Git push не запустил новый job. Неизменный запуск ничего не отправляет.

Cosign использует `/etc/ci/signing-config.json` без внешних signing services.
Настройка берётся из `ci.signing.configuration`. Verify использует публичный
ключ и `--insecure-ignore-tlog`: проверка подписи остаётся включена, публикация
в публичный transparency log отсутствует. TLS Registry проверяется штатно.
Описание формата — в [документации Sigstore](https://github.com/sigstore/cosign/blob/v3.1.4/doc/cosign_signing-config_create.md).

werf пакует `.helm`; образ собирает Buildx. `werf-giterminism.yaml` разрешает
только подготовленный CI файл `.ci-values.json`, общий giterminism не отключён.
Навигация и login используют обновляемые projected volumes. Сертификат читается
Nginx через переменную с нативным кешем; проверка ротации на живом Pod ещё нужна.

Для публикации выбранного Git-зеркала включите `PUBLISH_PORTFOLIO` и задайте
Secrets `PORTFOLIO_SOURCE_URL`, `PORTFOLIO_PUSH_URL`. Копирование выполняет CI
из management; публичный frontend записи не принимает.

## Подтверждённое состояние

Портал доставлен через Forgejo Actions: два теста приложения, native проверка
запуска собранного образа с ограниченными правами, Gitleaks, Trivy, build/push,
Cosign sign/verify и werf bundle publish/apply прошли. Trivy не обнаружил
HIGH/CRITICAL в использованном образе. Release приложения развёрнут на public/app.

Через SOCKS5 проверены login200, отсутствие/неверный пароль401, соответствие
навигации исходному плану и загрузка CSS/JS. В установленном Chrome с системным
PAC проверены 10 карточек, поиск и узкий экран; ошибок JavaScript нет.
Приватный backend недоступен другой public VM, socket гипервизора слушает
loopback, публичный frontend отклоняет TLS-имя портала.

Этап приложений применяет готовый firewall каждого обычного worker перед
публикацией ресурсов. Рабочие правила роутеров остаются во владении k0s.
Повторное применение ресурсов портала завершилось с `changed=0`.

Нативная локальная проверка private signing config не требует общего Vault:

```console
cd ansible
ansible-playbook tests/ci-signing-macos.yml
```

Она использует закреплённый Mac CLI и одноразовый ключ, проверяет подпись и
отклонение изменённого файла при запрещённом исходящем доступе.

Проверка восстановления CI credentials на работающем внутреннем Forgejo:

```console
cd ansible
ansible-playbook tests/ci-identities.yml --ask-vault-pass
```

Тест создаёт и удаляет собственные временные token/runner. Проверяет повторный
запуск с offline runner, восстановление удалённых credentials и обработку другого
UUID за сохранённым ID. Рабочие регистрации и общий Vault не изменяются.

Проверка восстановления репозитория из сохранённой локальной истории:

```console
cd ansible
ansible-playbook tests/ci-repository-replay.yml --ask-vault-pass
```

Она создаёт отдельный приватный репозиторий без workflow, сохраняет локальный
коммит, пересоздаёт только этот remote и проверяет обычный push. Временные
репозиторий и checkout удаляются после проверки.
