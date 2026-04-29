<!--
Slides em formato Marp/markdown. Para gerar PDF:
  npx @marp-team/marp-cli@latest slides.md --pdf
ou abrir no VS Code com a extensão Marp.
-->
---
marp: true
paginate: true
title: mini-SNMP — Trabalho Prático 2
---

# mini-SNMP

Sistema de gerenciamento de rede em C
sobre TCP, com MIB simplificada
e detecção de falhas.

Trabalho Prático 2 · Redes

---

## Arquitetura

- **Core** compartilhado: PDU codec, MIB estática, logging.
- **Agente**: servidor TCP multi-thread, coletores `/proc`.
- **Manager**: thread por agente, scheduler, CSV, detecção de falhas.
- Comunicação via TCP, ASCII delimitado por `\n`.

---

## Protocolo

```
VERSION|MSG_ID|TYPE|OID|VALUE\n
```

- Tipos: `GET`, `RESPONSE`, `ERROR`, `TRAP` (reservado).
- Texto delimitado: depurabilidade > densidade.
- `|` e `\n` reservados; codec valida na codificação.
- Códigos de erro: `NO_SUCH_OID`, `BAD_REQUEST`, `INTERNAL`, …

---

## MIB simplificada

| OID    | Métrica                | Fonte                |
|--------|------------------------|----------------------|
| 1.1.1  | CPU (%)                | `/proc/stat`         |
| 1.1.2  | Memória (%)            | `/proc/meminfo`      |
| 1.1.3  | Uptime (s)             | `/proc/uptime`       |
| 1.2.1  | Bytes in               | `/proc/net/dev`      |
| 1.2.2  | Bytes out              | `/proc/net/dev`      |
| 1.3.1  | Conexões TCP ativas    | `/proc/net/tcp{,6}`  |

Coletores são injetados em runtime via `mib_set_resolver`.

---

## Manager — fluxo

1. Carrega `agents.conf` (`<id> <host> <porta>`).
2. Para cada agente, dispara worker thread.
3. A cada `interval`: abre TCP → GET por OID → grava CSV → fecha.
4. Atualiza estado UP/DOWN; logs estruturados.
5. Renderiza tabela TUI (refresh 1 Hz).

---

## Detecção de falhas

`SO_RCVTIMEO/SO_SNDTIMEO` + contador de falhas consecutivas.

- Falha = `connect` recusado, timeout ou RESPONSE incompleto.
- DOWN ao atingir `--threshold` falhas consecutivas.
- UP ao primeiro RESPONSE válido.
- Esperado: $T_{det} \approx \text{threshold} \times \max(\text{interval}, \text{timeout})$.

---

## Latência (loopback)

| N    | p50 (µs) | p95 (µs) | mean (µs) |
|-----:|---------:|---------:|----------:|
| 1    | 342      | 342      | 342.0     |
| 10   | 128      | 213      | 135.0     |
| 100  | 132      | 183      | 136.9     |
| 1000 | 130      | 178      | 138.7     |

Convergência para ~130 µs por par GET/RESPONSE.

---

## Escalabilidade

| K agentes | CPU% manager | amostras |
|----------:|-------------:|---------:|
| 1         | 0.00         | 30       |
| 5         | 0.25         | 150      |
| 10        | 0.75         | 300      |
| 20        | 1.75         | 600      |

`interval=1 s`, duração 4 s — crescimento linear ≈ 0.09 %/agente.

---

## Detecção de falha

`interval=1 s`, `timeout=500 ms`, `threshold=3`.

| trial | tempo até DOWN |
|------:|---------------:|
| 1     | 2.925 s        |
| 2     | 2.906 s        |
| 3     | 2.910 s        |

Variância < 20 ms. Próximo ao mínimo teórico.

---

## Limitações

- TRAPs não implementados (agente reativo).
- Sem autenticação/criptografia.
- Baseline de CPU reinicia por processo.
- Histórico só no CSV — sem agregação em memória.

---

## Demonstração

```bash
make
./bin/agent 9001 --id node-a &
./bin/agent 9002 --id node-b &
./bin/manager agents.conf --interval 2 --threshold 3
# kill node-b → DOWN dentro de ~6 s
cat history.csv
```

---

## Obrigado

Repositório com código, scripts e relatório.
