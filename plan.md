# Plano de Implementação — Mini-SNMP em C

## Contexto
Trabalho prático (IME/USP) que pede a implementação, em arquitetura modular, de um sistema de gerenciamento de rede inspirado no SNMP, composto por **Manager** e **Agents** comunicando-se por sockets, com MIB simplificada, monitoramento periódico e detecção de falhas. O diretório atual contém apenas o PDF do enunciado — não há código pré-existente. A linguagem escolhida é **C** (POSIX sockets, pthreads).

## Decisões de Projeto (confirmadas)
1. **Transporte:** **TCP** (conforme diagrama do enunciado). Manager abre conexão por ciclo de coleta (ou mantém persistente) e fecha após RESPONSE.
2. **Formato de mensagem:** **texto ASCII delimitado** — `VERSION|MSG_ID|TYPE|OID|VALUE\n`. Terminador `\n` delimita PDUs no stream TCP.
3. **Plataforma:** **Linux** (uso de `/proc`).
4. **Bônus:** nenhum nesta fase — foco no escopo obrigatório.
3. **MIB simplificada (OIDs estilo `1.x.y`):**
   - `1.1.1` CPU (%) · `1.1.2` Memória (%) · `1.1.3` Uptime (s)
   - `1.2.1` Tráfego In (bytes) · `1.2.2` Tráfego Out (bytes)
   - `1.3.1` Conexões ativas
4. **Coleta no agente (Linux):** leitura de `/proc/stat`, `/proc/meminfo`, `/proc/uptime`, `/proc/net/dev`, `/proc/net/tcp`.
5. **Manager:** thread/loop por agente configurado via arquivo `agents.conf`; armazena histórico em CSV; detecta falha após N timeouts consecutivos.
6. **Tipos de PDU:** `GET`, `RESPONSE`, `ERROR`, e (bônus) `TRAP`.

## Estrutura de Diretórios Proposta
```
SNMP/
├── Makefile
├── README.md
├── agents.conf
├── include/
│   ├── protocol.h     # PDU, codificação/decodificação
│   ├── mib.h          # tabela OID→handler
│   └── util.h         # logging, timing, parsing
├── src/
│   ├── protocol.c
│   ├── mib.c
│   ├── util.c
│   ├── agent/
│   │   ├── agent_main.c
│   │   └── collectors.c   # /proc readers
│   └── manager/
│       ├── manager_main.c
│       ├── scheduler.c
│       ├── storage.c      # CSV/log
│       └── failure.c
└── experiments/
    ├── run_latency.sh
    ├── run_scalability.sh
    └── results/
```

## Tasks (unidades de trabalho — apresentadas para decisão humana)

### Fase 1 — Fundação
- **T1.** Criar Makefile, estrutura de diretórios, `util.[ch]` (log com timestamp, parsing seguro de string).
- **T2.** Definir e documentar o **protocolo** (`protocol.h/.c`): formato de PDU, encode/decode, versão, message-id, tipos GET/RESPONSE/ERROR.
- **T3.** Definir a **MIB** (`mib.h/.c`): tabela estática `{oid, descrição, handler()}`, função `mib_lookup(oid)`.

### Fase 2 — Agente
- **T4.** Servidor TCP do agente: `socket/bind/listen/accept` em loop; para cada conexão, ler PDU(s) até `\n`, despachar para handler da MIB, responder `RESPONSE`/`ERROR`. Tratamento concorrente com `fork` ou thread por conexão.
- **T5.** Coletores em `collectors.c`: CPU (delta entre amostras de `/proc/stat`), memória (`/proc/meminfo`), uptime, tráfego de rede (`/proc/net/dev`), conexões ativas (`/proc/net/tcp`).
- **T6.** CLI do agente: `./agent <porta> [--id NODE_ID]`.

### Fase 3 — Manager
- **T7.** Carregamento de `agents.conf` (linhas `id host porta`).
- **T8.** Scheduler: para cada agente, abrir conexão TCP em intervalos regulares e enviar GETs para um conjunto de OIDs; `connect` não-bloqueante ou `SO_RCVTIMEO`/`SO_SNDTIMEO` para detectar timeouts.
- **T9.** Armazenamento: append em `history.csv` (`timestamp,agent,oid,value`) e log textual.
- **T10.** Detecção de falhas: contador de timeouts; após K falhas consecutivas marca agente como DOWN; volta a UP ao receber RESPONSE.
- **T11.** Saída em terminal: tabela atualizada com estado de cada agente (curses opcional, padrão `printf` + `clear`).

### Fase 4 — Avaliação Experimental
- **T12.** Script de **latência**: medir RTT médio do GET/RESPONSE com N=1, 10, 100 OIDs.
- **T13.** Script de **escalabilidade**: subir 1, 5, 10, 20 agentes locais (portas distintas) e medir CPU do manager + perda de pacotes.
- **T14.** Script de **falha**: derrubar agente durante execução e medir tempo até detecção.
- **T15.** Script de **overhead**: capturar com `tcpdump` e calcular bytes/segundo por agente.

### Fase 5 — Entrega
- **T16.** Documentação: `README.md` com instruções de build/run, descrição do protocolo, tabela da MIB.
- **T17.** Relatório técnico (PDF) cobrindo os 8 itens do enunciado §8.
- **T18.** Slides de apresentação.

### Bônus
Nenhum nesta fase (pode ser reavaliado após escopo obrigatório completo).

## Arquivos Críticos a Criar
- `include/protocol.h`, `src/protocol.c` — coração da comunicação.
- `include/mib.h`, `src/mib.c` — tabela de OIDs.
- `src/agent/agent_main.c`, `src/agent/collectors.c`.
- `src/manager/{manager_main,scheduler,storage,failure}.c`.
- `Makefile`, `agents.conf`, `README.md`.

## Verificação End-to-End
1. `make` produz `bin/agent` e `bin/manager`.
2. Em 3 terminais: `./bin/agent 9001`, `./bin/agent 9002`, `./bin/manager agents.conf`.
3. Manager exibe métricas atualizadas a cada N segundos.
4. Matar um agente → manager marca DOWN dentro do timeout configurado.
5. `cat history.csv` mostra séries temporais.
6. Rodar scripts em `experiments/` e gerar gráficos para o relatório.

## Pontos Pendentes
- Confirmar a tabela de OIDs/métricas (CPU, Memória, Uptime, Tráfego In/Out, Conexões) — pode ser ajustada antes do início.
- Após aprovação, criar `plan.md` no repositório espelhando este conteúdo (o arquivo atual é o gerenciado pelo modo /plan).
