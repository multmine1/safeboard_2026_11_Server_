# Доступ к сервисам

Подготовленные приложения запускаются через `python scripts/applications.py`.
Настройка Vault описана в [secrets.md](secrets.md). На живом кластере работают
прикладные компоненты из исходных JSON; адреса проверены через SSH с Mac.

| VM | Компоненты |
|---|---|
| management/control | CNPG operator, Sealed Secrets, local-path-provisioner |
| infra/db | PostgreSQL 17, NFSv4, VictoriaMetrics, VictoriaLogs |
| infra/forgejo | внутренний Forgejo с Actions |
| infra/observe | Grafana, vmalert, Alertmanager, OpenTelemetry Collector |
| public/db | PostgreSQL, отдельный Forgejo Container Registry, NFSv4, Valkey 8 |
| public/mirror | отдельный Forgejo для выбранных репозиториев портфолио |
| public/edge | Nginx и HTTPS-вход портфолио |
| public/app | Nextcloud, NFS-файлы на public/db, PostgreSQL и Valkey с TLS |
| public/mail | Stalwart, SSD для сообщений, PostgreSQL public/db для метаданных |
| management/ci | runner, werf, Docker/Compose/Buildx, Gitleaks, Trivy, Cosign, uv; runner подключён, pipeline ещё не принят |

Собственное приложение ещё не выбрано. Для его VM подготовлены доступы к данным,
ServiceAccount и imagePullSecret; название и функции приложения не задаются.

## Доступ через SSH SOCKS5

На Mac один раз настройте системный PAC, затем загрузите vmctl-ключ в агент
и запустите SOCKS5:

```console
python scripts/browser_proxy.py
python scripts/agent.py
python scripts/socks.py
```

Safari и Chromium используют системные настройки; в Firefox выберите системный
proxy. Домен стенда открывается через SOCKS5 с удалённым DNS, остальные сайты —
напрямую. Команда и notebook выводят URL из `configs/generated/ansible/local-access.json`.
SSH Config выбирает LAN, затем bastion; `--host` переопределяет alias.
Для Mac только с SSH получите нативную команду через `--print-command`.
Остановка SOCKS5 — `Ctrl+C`; отключение системного PAC описано в корневом README.

Вместо отдельных локальных портов используются действительные порты сервисов.
HTTPS проверяет CA проекта и DNS-имя; подмены `/etc/hosts` не требуются.
SSH разрешает только точные IP/FQDN:port из общего плана. Подключение проходит
от гипервизора через WAN главного роутера, DNAT и роутер целевой сети.
Это не открывает сервисы в интернет и не добавляет обходные NIC.
Cockpit расположен на гипервизоре и доступен по
URL из раздела `cockpit`, который выводит `scripts/socks.py`, независимо от состояния k0s.

## Портфолио

Публичный frontend: URL раздела `https` из `scripts/socks.py`.
Управляющий backend: тот же host, порт `13000`; он доступен только через SSH
и использует отдельный origin. Для обычного просмотра используйте frontend.
Внешние DNS и перенаправления 80/443 физического роутера
на WAN-IP главного router VM настраиваются отдельно.

Nginx разрешает GET/HEAD/OPTIONS и POST только для Git `git-upload-pack`.
Push, API-запись и вход через POST с внешнего frontend блокируются. Избранные
репозитории принимает backend от CI в management. Между infra и public для этого
не добавляется прямое разрешение. Nginx проверяет CA и DNS-имя backend-сертификата.

## Наблюдаемость

VictoriaMetrics и VictoriaLogs сохраняют данные на infra/db: соответственно
NVMe и SSD. Retention задан в workload JSON. Grafana получает готовые URL
VictoriaMetrics и VictoriaLogs; плагин VictoriaLogs закреплён по версии. Её конфигурация и datasource восстанавливаются из JSON;
сохранение произвольных ручных dashboards в локальной БД Grafana не включено.

OpenTelemetry Collector собирает host metrics и Pod logs своего узла observe,
передаёт их в VictoriaMetrics/Logs. Он не объявляется сборщиком всех VM.
VictoriaMetrics также собирает собственные метрики. vmalert читает их и передаёт
алерты в Alertmanager. Alertmanager отправляет уведомления через Stalwart в локальный ящик postmaster;
проверка реальной доставки пройдена. SMTP пароль поступает из отдельного Secret-файла.

Новые источники telemetry требуют явных сетевых разрешений и настроек
receiver/exporter. Открывать публичным worker доступ к произвольным данным infra
для этого не требуется.

## Nextcloud

Адрес `https-cloud` выводит `scripts/socks.py`. Файлы хранятся на SSD public/db
через NFSv4; отдельные PostgreSQL credentials и Valkey ACL лежат в общем Vault.
Квота — 128 GB. Загрузка/скачивание и сохранность после замены Pod проверены.
Успешный запуск CronJob и HTTPS-запрос из приложения проверены. Обновление
версии и восстановление приложения ещё не приняты.

## Почта

Stalwart 1.0.3 развёрнут на отдельной public/mail VM с SSD для сообщений
и PostgreSQL public/db для метаданных. SMTP/IMAP с проверкой TLS и запрет анонимного relay проверены.
Штатный init-контейнер применяет готовый NDJSON через stalwart-cli 1.0.14;
основной процесс не получает recovery-пароль. Публичные SMTP/SMTPS/IMAPS
отделены от приватного административного HTTPS и внутреннего STARTTLS.

Новые учётные записи готовит `ansible/playbooks/mail-credentials.yml` в общем
Vault: ящик postmaster, отдельный admin и отправители forgejo/nextcloud/monitoring/system.
SMTP-настройки Forgejo/Nextcloud/Alertmanager применены. Nextcloud Mail 5.13.1
подключён; его IMAP/SMTP и доставка уведомлений Nextcloud/Alertmanager проверены. DNS/PTR и пробросы
внешнего роутера остаются ручным этапом оператора.
Используется [штатный процесс декларативного развёртывания Stalwart](https://stalw.art/docs/configuration/declarative-deployments/).

Подготовленный startup hook использует штатный `occ security:certificates:import`,
сохраняя публичные корневые CA, и применяет квоту/режим cron также к существующей
установке. Список `trusted_proxies` получается из разрешённых клиентов backend:
адрес Nginx и его Pod-диапазон. Apache не переписывает адрес клиента до проверки
Nextcloud. Правки применены и проверены живым Ansible playbook с Mac через control plane.
