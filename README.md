# DevOps — переносимые шаблоны инфраструктуры

Открытый исходный комплект для генерации конфигураций виртуальных машин и
сетей. Здесь находятся C++-генератор, Ansible, OpenTofu, настройки k0s,
декларации приложений и демонстрационные JSON. **Это не готовая конфигурация
чужого сервера.** Адреса, имена, UUID, публичные ключи и локальные параметры
необходимо задать для своего стенда. Актуальность комплекта: ноябрь 2026 года.

## Что входит

| Каталог | Назначение |
| --- | --- |
| `cpp/` | Генератор C++20, CMake, модульные и интеграционные тесты |
| `configs/source/demo/` | Обезличенный пример топологии VM и сетей |
| `configs/source/workloads/demo/` | Примеры описаний приложений и Kubernetes-ресурсов |
| `configs/source/instance.example.json` | Шаблон локальных параметров, адресов и SSH |
| `configs/source/hypervisor/` | Общие политики гипервизора и установки ОС |
| `configs/vault.example.yml` | **Пустая схема** секретов; реальные значения не включены |
| `ansible/` | Playbook, роли, проверки и зависимости для настройки хоста и гостей |
| `tofu/requirements/` | Зафиксированный provider libvirt и список провайдеров |
| `k0s/` | Требования для Kubernetes, сетевых политик и сервисов |
| `web/admin-portal/` | Шаблон внутреннего веб-портала, Helm и тесты |
| `scripts/` | Управляющие Python-скрипты для отдельных этапов |
| `notebooks/workbench.ipynb` | Безопасные локальные проверки структуры проекта |
| `environment/requirements/` | Файлы Conda для macOS arm64 и Linux x86_64 |

Архитектура: управляющая машина (macOS/Linux) → SSH → Linux-гипервизор с
QEMU/KVM/libvirt → VM Debian 13 через cloud-init → Ansible и k0s. Сеть
описывается декларативно, маршрутизация и изоляция выполняются с помощью
nftables, CoreDNS и политик Kubernetes. OpenTofu отвечает за ресурсы
гипервизора; Ansible — за настройку ОС и сервисов; C++ преобразует исходный
JSON в машинные конфигурации. Приложения из demo — **примеры**, их не требуется
разворачивать все одновременно.

В исходных шаблонах присутствуют версии CMake 4.4.5, C++20,
`nlohmann_json` 3.12.0, GoogleTest 1.18.0, Ansible Core 2.21.5,
libvirt-provider 0.9.9. Для сервера предполагаются Ubuntu 26.04,
QEMU/KVM/libvirt и гостевые Debian 13. Указанные версии **зафиксированы во
входных файлах**, а не перепроверены на каждом целевом компьютере.

## 1. Подготовка контроллера

Используйте существующую Conda/Anaconda, если она уже установлена. Для
создания окружения на основе переносимого YAML выполните из корня проекта:

```bash
conda env create -f environment/requirements/environment.yml
conda activate skeleton
ansible-galaxy collection install -r ansible/requirements/collections.yml \
  -p environment/.tools/ansible/collections
```

Для точного восстановления версий также приложены
`environment/requirements/conda-lock.yml` и
`environment/requirements/lab/conda-lock.yml` (формат **conda-lockfiles**).
Если в установленном Conda есть поддержка `conda-lockfiles`, допускается:

```bash
conda create -n skeleton --file environment/requirements/conda-lock.yml
```

Не создавайте второе окружение с тем же именем без необходимости. Для учебного
Python/R-окружения независимо используется
`environment/requirements/lab/environment.yml`. В
`environment/requirements/bootstrap.json` содержатся URL и контрольные суммы
установщиков Anaconda; это метаданные, а не установленные программы.

На целевом Linux-хосте **заранее** должны работать QEMU/KVM и libvirt;
OpenTofu не заменяет их установку. Подключение по SSH и доступ к libvirt
требуют предварительной настройки прав. Не копируйте секреты на сервер вместе
с публичным репозиторием.

## 2. Локальные параметры и секреты

```bash
cp configs/source/instance.example.json configs/source/instance.json
chmod 600 configs/source/instance.json
```

Отредактируйте `configs/source/instance.json`: домен, адреса, сеть управления,
SSH-алиасы, расположение проекта, публичные ключи, диски и лимиты ресурсов.
Все поля с `REPLACE_WITH_...` требуют явной проверки. Значения из demo нельзя
переносить в рабочую сеть без адаптации. `configs/source/instance.json`
исключён из Git.

Пример структуры Vault — `configs/vault.example.yml`. Создайте отдельный
зашифрованный файл `configs/secrets/vault.yml` через `ansible-vault create`,
используя шаблон полей. Не копируйте туда пустой шаблон как будто это рабочие
секреты. Приватные SSH-ключи, пароли, сертификаты, kubeconfig, файл state и
резервные копии хранятся **только локально**, вне Git. Операционные данные в
архив намеренно не включены.

## 3. Сборка генератора и тесты

```bash
cd cpp
cmake --preset default
cmake --build ../environment/.tools/build/cpp
ctest --test-dir ../environment/.tools/build/cpp --output-on-failure
cd ..
```

Вход генератора: директория с `site.json`, `*.network.json`, `*.vm.json` и
отдельными конфигурациями workloads. Выход: OpenTofu/Ansible/k0s-манифесты и
визуализация топологии в `configs/generated/`.

```bash
environment/.tools/build/cpp/config-generator \
  configs/source/demo configs/generated
```

Выходные файлы могут содержать вычисленные адреса и другую внутреннюю
информацию. Поэтому `configs/generated/` исключён из Git. Перед генерацией
проверьте заполненность локального `instance.json`, корректность SSH и
параметров demo. Генерация сама по себе **не развёртывает** VM.

## 4. Проверка без применения инфраструктуры

```bash
python -m compileall -q scripts
python -m json.tool configs/source/demo/site.json >/dev/null
```

Для проверки структуры откройте `notebooks/workbench.ipynb` через JupyterLab.
Его ячейки только читают файлы и отображают состав примера; выполнение
инфраструктурных операций не заложено.

После успешной генерации доступна проверка OpenTofu:

```bash
tofu -chdir=configs/generated/tofu init -backend=false
tofu -chdir=configs/generated/tofu validate
```

`init` может скачивать провайдеры. Реальный `plan` требует доступа к libvirt,
а `apply`, Ansible playbook для установки, `python run.py` и скрипты из
`scripts/` **могут менять живую инфраструктуру**. Не выполняйте их для
проверки публичного архива. Сначала прочитайте параметры, проверьте diff
планируемых изменений и доступы на своём контроллере.

## 5. Что намеренно не включено

- Личные `instance.json` и `vault.json`, закрытые SSH-ключи, пароли,
  сертификаты, токены, реальные inventory и kubeconfig.
- `configs/generated/`, `configs/secrets/`, `.terraform/`, state/plan,
  журналы, кэши, временные тесты и локальные образы.
- `AGENTS.md`, история команд и индивидуальные указания AI-агентам.
- Локальное Python/Conda-окружение `environment/.tools/` — оно создаётся на
  целевой платформе и не предназначено для переноса через Git.

Дополнительные исходные материалы, которых не оказалось ни в одном из
переданных ZIP, описаны в [MISSING_FILES.md](MISSING_FILES.md).


