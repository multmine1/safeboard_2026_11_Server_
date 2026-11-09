# Vault, сертификаты и Registry

Команды выполняются из корня проекта на Mac в активном окружении. Пароли вводятся в
Terminal, единый `configs/secrets/vault.yml` редактируется через Ansible Vault.
PKI, доверие и Secrets активного контура применены на VM. Примеры ниже содержат
только названия полей.

## Собственная CA

После генерации:

```console
python scripts/pki.py
```

Команда запрашивает пароль Vault, создаёт отсутствующую CA и сертификаты в памяти
штатными модулями `community.crypto`. Приватные ключи сохраняются только внутри
`configs/secrets/vault.yml`, секция `vault_store.pki`. Публичный сертификат — `configs/secrets/ca.crt`.
Существующие ключи сохраняются; срок и окно обновления заданы в исходном JSON.
Перед истечением CA требуется отдельное обновление доверия.

```console
python scripts/trust.py
```

Этот этап устанавливает публичную CA работающим клиентам Registry, объявленным
в общей политике, настраивает containerd 2 и Docker. Его можно выполнить перед
установкой k0s; после включения CI VM повторите этап для нового клиента.
В контейнеры приложений доверие передаётся их Secret/volume-конфигурацией.

На Mac импортируйте публичную CA в Keychain и подтвердите доверие для HTTPS.
Публичный сертификат уже находится в `configs/secrets/ca.crt` после локального
`python scripts/pki.py`. Приватный ключ CA остаётся в общем Vault. Собственная CA требует настройки
доверия на каждом клиенте, в том числе при показе публичного HTTPS другим людям.

## Значения приложений

```console
ansible-vault edit configs/secrets/vault.yml
```

Секция `vault_store.applications` — словарь namespace → имя Secret → его `type` и `stringData`.
Настоящие имена namespace и требуемых Secrets доступны в
`configs/generated/k0s/applications/deployment.json`.

Пример содержимого `vault_store.applications` для редактора Vault:

```yaml
PUBLIC_NAMESPACE:
  registry-admin:
    type: Opaque
    stringData:
      username: registry-admin
      password: ВВЕСТИ_В_VAULT
  registry-postgres:
    type: kubernetes.io/basic-auth
    stringData:
      username: registry
      password: ВВЕСТИ_В_VAULT
```

Для администратора Forgejo выберите допустимое имя пользователя: `admin`
зарезервировано приложением. В текущем развёртывании используется имя из локальной конфигурации.

Сначала достаточно заполнить Secrets для выбранных приложений. Первичный запуск:

```console
python scripts/applications.py registry forgejo
```

C++ заранее рассчитывает их зависимости и порядок. Выборочный запуск сохраняет
остальные опубликованные наборы. CNPG создаёт Secrets владельцев начальных БД и
свои PostgreSQL CA самостоятельно; их не нужно дублировать в Vault.

После готовности Registry создайте через его интерфейс отдельную учётную запись
и токен только для чтения пакетов. Дополните Vault, затем примените полный набор:

```console
ansible-vault edit configs/secrets/vault.yml
python scripts/applications.py
```

| Secret | Namespace сети | Поля `stringData` |
|---|---|---|
| `forgejo-admin` | infra | `username`, `password` |
| `portfolio-admin` | public | `username`, `password` |
| `registry-admin` | public | `username`, `password` |
| `registry-postgres` | public | `username: registry`, `password` |
| `grafana-admin` | infra | `admin-user`, `admin-password` |
| `valkey-auth` | public | `users.acl`, штатный ACL Valkey |
| `registry-pull` | public | `.dockerconfigjson`, тип `kubernetes.io/dockerconfigjson` |
| `ci-signing` | management | `cosign.key`, `cosign.pub`; требуется при включении CI |

`users.acl` хранится многострочной строкой YAML с настоящими переносами строк:

```yaml
users.acl: |
  user default off
  user application on >REPLACE_WITH_PASSWORD ~* &* +@all -@dangerous
```

Для `registry-pull` JSON содержит `auths` с адресом Registry из подготовленных
`registry_trust.authority`, именем пользователя и токеном в поле `password`.
Приложения используют ServiceAccount `application` и этот imagePullSecret.
Приватные значения не включаются в Dockerfile, Helm values или Git.

Сертификаты `*-tls` и Secrets CA создаются из `vault_store.pki` автоматически.
В `vault_store.applications` их дублировать не требуется. Ansible передаёт значения в штатный `kubeseal`
через stdin, применяет только SealedSecret, ждёт его текущую ревизию и готовность.
При изменении управляемого Secret перезапускаются его Deployment-потребители.
Сравнение учитывает тип и данные Secret, не служебные аннотации контроллеров.

Обновление: `scripts/pki.py` → `scripts/trust.py` при смене CA →
`scripts/applications.py`. Ручное изменение CNPG-паролей требует согласованного
обновления потребителей; их автоматическое отслеживание отдельным watcher не
устанавливается.

Сохраняйте зашифрованный `vault.yml` и резервную копию ключей Sealed Secrets отдельно
от репозитория. Потеря ключа контроллера требует повторного запечатывания из
Vault; сохранённые зашифрованные SealedSecret сами по себе не заменяют backup.
