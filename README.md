## Link do Repositório

https://github.com/MAC0352/EP2

# mini-SNMP — Trabalho Prático 2 (Redes)

Implementação em C (POSIX) de um sistema de gerenciamento de rede
inspirado no SNMP, composto por um **agente** que expõe métricas do
host via `/proc` e um **manager** que coleta essas métricas
periodicamente sobre TCP, mantém histórico em CSV e detecta agentes
indisponíveis.

## Build

```bash
make            # produz bin/agent, bin/manager, bin/latency
make clean
```

Requisitos: `gcc`/`clang` com C11, `pthread`, Linux com `/proc`.

## Execução rápida

```bash
# Em terminais separados:
./bin/agent 9001 --id node-a
./bin/agent 9002 --id node-b
./bin/manager agents.conf --interval 5 --timeout-ms 1000 --threshold 3
```

O manager imprime uma tabela atualizada a cada segundo e grava
amostras em `history.csv`. `Ctrl-C` encerra de forma limpa.

### Flags

`bin/agent <porta> [--id NODE_ID] [-v]`
- `--id` define o identificador exibido no log; `-v` ativa nível DEBUG.

`bin/manager <agents.conf> [--interval N] [--timeout-ms N] [--threshold K] [--no-tui] [-v]`
- `--interval`: período de coleta por agente (s, default 5).
- `--timeout-ms`: timeout de `connect`/`recv` (default 2000).
- `--threshold`: falhas consecutivas antes de marcar DOWN (default 3).
- `--no-tui`: desabilita refresh de tela (útil em scripts).

`bin/latency <host> <port> <N> [oid]`
- Cliente de medição: abre uma conexão e dispara `N` GETs sequenciais,
  imprimindo min/p50/p95/max/mean em µs.

## Protocolo

PDU em ASCII, terminada por `\n`:

```
VERSION|MSG_ID|TYPE|OID|VALUE\n
```

- `VERSION` — inteiro; atualmente `1`.
- `MSG_ID` — inteiro 32-bit, escolhido pelo manager para correlacionar
  request/response.
- `TYPE` — `GET`, `RESPONSE`, `ERROR`, `TRAP` (TRAP reservado para
  futura extensão).
- `OID` — string dotada (ex.: `1.1.1`).
- `VALUE` — vazio em GET; valor numérico/textual em RESPONSE; código
  de erro em ERROR (`<num> <slug>`).

Os caracteres `|` e `\n` são reservados e não podem aparecer em
nenhum campo. O codec está em `src/protocol.c`.

### Códigos de erro

| Código | Slug          | Significado                          |
|------:|---------------|--------------------------------------|
| 0     | `OK`          | (não usado em ERROR)                  |
| 1     | `NO_SUCH_OID` | OID não pertence à MIB                |
| 2     | `BAD_REQUEST` | PDU malformado ou tipo inesperado     |
| 3     | `INTERNAL`    | falha ao ler fonte do dado            |
| 4     | `UNSUPPORTED` | operação não suportada                |

## MIB

Tabela estática com 6 OIDs (`src/mib.c`):

| OID    | Métrica                         | Fonte (`/proc/...`)              |
|-------:|---------------------------------|----------------------------------|
| 1.1.1  | CPU usage (%)                   | `stat` (delta entre amostras)    |
| 1.1.2  | Memory usage (%)                | `meminfo` (`MemAvailable`)       |
| 1.1.3  | Uptime (segundos)               | `uptime`                         |
| 1.2.1  | Tráfego de rede in (bytes)      | `net/dev` (soma, exceto `lo`)    |
| 1.2.2  | Tráfego de rede out (bytes)     | `net/dev` (soma, exceto `lo`)    |
| 1.3.1  | Conexões TCP ativas             | `net/tcp` + `net/tcp6` ESTAB     |

A interface entre MIB e coletores é `mib_set_resolver`: o agente
injeta os handlers `/proc` em runtime sem mutar a tabela estática
mantida pelo core (compartilhada com o manager).

## Configuração de agentes (`agents.conf`)

Uma linha por agente, no formato `<id> <host> <porta>`. Linhas
iniciadas com `#` são comentários.

```
node-a 127.0.0.1 9001
node-b 127.0.0.1 9002
```

## Armazenamento

`history.csv` é criado/anexado pelo manager:

```
timestamp,agent,oid,value
1777420519,node-a,1.1.1,11.99
...
```

`timestamp` é epoch UNIX. Append é thread-safe (`storage.c`).

## Estrutura

```
include/{util,protocol,mib}.h     # API do core
src/{util,protocol,mib}.c          # core compartilhado
src/agent/{agent_main,collectors}.c
src/manager/{manager_main,scheduler,storage,failure}.c
src/tools/latency.c                # cliente de medição
experiments/run_*.sh               # scripts de avaliação
experiments/results/*.csv          # saídas dos experimentos
```

## Avaliação experimental

Quatro scripts em `experiments/` produzem CSVs em `experiments/results/`:

| Script               | Mede                                              |
|----------------------|---------------------------------------------------|
| `run_latency.sh`     | RTT GET/RESPONSE para N=1, 10, 100, 1000          |
| `run_scalability.sh` | CPU% do manager para K=1, 5, 10, 20 agentes       |
| `run_failure.sh`     | Tempo entre `kill` do agente e transição DOWN     |
| `run_overhead.sh`    | Bytes/s na conexão (via `tcpdump` em `lo`)        |

Resultados locais e análise em `report.md`.

## Observações

- TCP sem persistência: o manager abre uma conexão por ciclo de
  coleta e fecha após receber todas as respostas. Isso simplifica a
  detecção de falhas (qualquer erro de `connect`/`recv` conta) ao
  custo de overhead de handshake por ciclo.
- Concorrência no agente: uma `pthread` por conexão, detached.
- Concorrência no manager: uma `pthread` por agente; CSV
  serializado por mutex global.
