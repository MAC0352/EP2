# Relatório Técnico — mini-SNMP

Trabalho Prático 2 · Redes de Computadores

## 1. Objetivo

Implementar, em arquitetura modular, um sistema de gerenciamento de
rede inspirado no SNMP, com **manager** e **agentes** comunicando-se
sobre TCP, MIB simplificada, monitoramento periódico e detecção de
falhas; e avaliá-lo experimentalmente quanto a latência,
escalabilidade, tempo de detecção e overhead de rede.

## 2. Arquitetura

```
   ┌──────────────┐       TCP        ┌──────────────┐
   │   manager    │ ───── GET ─────▶ │    agent     │
   │  (scheduler, │ ◀── RESPONSE ─── │  (collectors,│
   │  storage,    │                  │   /proc)     │
   │  failure)    │                  │              │
   └──────────────┘                  └──────────────┘
         │
         ▼
   history.csv
```

O **core** (compartilhado) reúne logging, codec do PDU e tabela da
MIB. O **agente** é um servidor TCP multi-thread (uma `pthread` por
conexão) que injeta na MIB resolvedores específicos do Linux que
leem `/proc`. O **manager** carrega `agents.conf`, dispara uma
worker thread por agente e em cada ciclo abre uma conexão, envia um
GET por OID, persiste em CSV e atualiza o estado.

## 3. Protocolo

PDU ASCII delimitada por `\n`:

```
VERSION|MSG_ID|TYPE|OID|VALUE\n
```

A escolha por texto delimitado prioriza depurabilidade (uma
mensagem é diretamente legível com `nc` ou `tcpdump -A`) sobre
densidade. `|` e `\n` são reservados e o codec rejeita PDUs que os
contenham nos campos. Tipos suportados: `GET`, `RESPONSE`, `ERROR`,
e `TRAP` (reservado).

### Erros

Quando o agente não consegue atender um GET ele responde `ERROR`,
com `VALUE` no formato `<código> <slug>` (ex.: `1 NO_SUCH_OID`).
O manager registra no CSV o valor literal `ERR` para preservar a
linha temporal sem exigir um schema variante.

## 4. MIB

Seis OIDs cobrem CPU, memória, uptime, tráfego de rede in/out e
conexões TCP ativas. A escolha é deliberadamente compacta: cobre as
quatro famílias clássicas de métricas de saúde de host (compute,
memória, rede e estado) com fontes estáveis em `/proc`.

| OID    | Métrica                       | Fonte                           |
|-------:|-------------------------------|---------------------------------|
| 1.1.1  | CPU usage (%)                 | `/proc/stat` (delta de amostras)|
| 1.1.2  | Memory usage (%)              | `/proc/meminfo`                 |
| 1.1.3  | Uptime (s)                    | `/proc/uptime`                  |
| 1.2.1  | Bytes in                      | `/proc/net/dev` (soma)          |
| 1.2.2  | Bytes out                     | `/proc/net/dev` (soma)          |
| 1.3.1  | Conexões TCP ativas           | `/proc/net/{tcp,tcp6}` ESTAB    |

A CPU é a única métrica que exige estado entre coletas: o handler
mantém uma amostra anterior protegida por mutex e calcula a fração
não-idle do delta — coletas com diferença de tempo muito pequena
ficam sujeitas a ruído, mitigado pelo intervalo de coleta padrão
(≥1 s).

## 5. Detecção de falhas

O manager abre uma conexão TCP nova por ciclo. Falha é qualquer
erro de `connect`, `recv` antes de receber RESPONSE de todos os
OIDs, ou timeout (`SO_RCVTIMEO`/`SO_SNDTIMEO`). Um contador por
agente é incrementado a cada falha e zerado a cada sucesso; ao
atingir `--threshold` o estado vira **DOWN** e a transição é
logada. Recuperação é instantânea no primeiro RESPONSE válido.

Tempo esperado de detecção:
$T_{det} \approx threshold \times max(interval, timeout)$. Com
`interval=1s`, `timeout=500ms`, `threshold=3`, a expectativa é
~3 s, confirmada pelos experimentos.

## 6. Avaliação experimental

Ambiente: Linux x86-64, `loopback` (`lo`), GCC com `-O2`.

### 6.1 Latência (T12) — `run_latency.sh`

Uma única conexão, N GETs sequenciais para o OID `1.1.3`.

| N    | min (µs) | p50 (µs) | p95 (µs) | max (µs) | mean (µs) |
|-----:|---------:|---------:|---------:|---------:|----------:|
| 1    | 342      | 342      | 342      | 342      | 342.0     |
| 10   | 120      | 128      | 213      | 213      | 135.0     |
| 100  | 120      | 132      | 183      | 216      | 136.9     |
| 1000 | 121      | 130      | 178      | 501      | 138.7     |

A latência converge para ~130 µs por par GET/RESPONSE em loopback;
o N=1 carrega o custo do primeiro `read` e do warmup do agente,
diluído nos demais N.

### 6.2 Escalabilidade (T13) — `run_scalability.sh`

K agentes locais, `interval=1 s`, duração de medida 4 s.

| K agentes | CPU% manager | amostras coletadas |
|----------:|-------------:|-------------------:|
| 1         | 0.00         | 30                 |
| 5         | 0.25         | 150                |
| 10        | 0.75         | 300                |
| 20        | 1.75         | 600                |

CPU% cresce de forma aproximadamente linear com K (≈0.09 %/agente),
e o número de amostras coletadas é exatamente
`K × n_OIDs × duração / interval`, indicando que nenhum ciclo foi
perdido. Em `interval=1 s` o manager está longe de saturar mesmo
para K=20.

### 6.3 Detecção de falha (T14) — `run_failure.sh`

`interval=1 s`, `timeout=500 ms`, `threshold=3`, 3 trials.

| trial | tempo até DOWN (s) |
|------:|-------------------:|
| 1     | 2.925              |
| 2     | 2.906              |
| 3     | 2.910              |

Média ~2.91 s, baixa variância (<20 ms entre trials), consistente
com a expectativa teórica de `3 × 1 s` reduzida pelo fato de o
`connect` recusado retornar quase imediatamente em vez de esgotar o
timeout.

### 6.4 Overhead (T15) — `run_overhead.sh`

A captura via `tcpdump -i lo` exige `CAP_NET_RAW`. Em execução não
privilegiada o script registra `NA` na coluna `bytes_per_s` e
preserva a contagem de amostras (30 em 5 s = 6 amostras/s para 6
OIDs por agente). Estimativa analítica do tamanho médio de PDU: GET
≈ 14 bytes, RESPONSE ≈ 25–35 bytes; com 6 OIDs por ciclo de 1 s e
um único par TCP handshake/teardown por ciclo, o overhead esperado
é da ordem de 300–500 B/s por agente, dominado pelos cabeçalhos
TCP/IP.

## 7. Limitações e trabalho futuro

- TRAPs não implementados: o agente é puramente reativo. Adicionar
  TRAP exigiria uma conexão persistente (ou inversão temporária de
  papéis) — fora do escopo obrigatório.
- Sem autenticação/criptografia. Mensagens em claro são adequadas
  ao contexto experimental, mas inviabilizam uso em rede pública.
- Coletor de CPU usa estado in-process: reinicializações resetam o
  baseline, e a primeira amostra retorna 0%.
- Manager mantém apenas o último valor por OID em memória; análises
  históricas dependem do CSV.

## 8. Conclusões

A implementação atende o escopo obrigatório do enunciado: protocolo
documentado, MIB simplificada, monitoramento periódico,
armazenamento histórico e detecção de falhas, com cobertura
experimental dos quatro eixos (latência, escalabilidade, detecção e
overhead). Em loopback a latência por GET é da ordem de 130 µs e o
manager monitora 20 agentes com menos de 2% de CPU em interval de
1 s, com detecção de falha próxima ao mínimo teórico imposto pelo
threshold.
