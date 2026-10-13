## kstuff + A53/PPR + BackPork — FW 5.00 (versão 1.1 experimental)

### Adicionado na versão 1.1

- **Modo de repouso:** após o console acordar, o PPR é verificado novamente e o monitor BackPork registra de novo o `SceSysCore`.
- **BackPork nativo:** integrado diretamente ao código-fonte do carregador, em um único ELF. Monta `app0/fakelib` antes de executar jogos compatíveis e desmonta ao fechá-los.

Este pré-lançamento reúne os patches A53/PPR, o kstuff e o BackPork em **um único ELF**. O BackPork é compilado no carregador, sem ELF embutido ou segundo processo: observa a criação de jogos, monta `app0/fakelib` sobre `common/lib` antes do `EXEC` e desmonta ao sair. Após o repouso, o monitor registra novamente `SceSysCore`; a notificação “BackPork active again!” aparece depois da primeira montagem antecipada bem-sucedida.

A versão 1.1 corrige uma primeira abertura intermitente observada em testes anteriores: o nome da pasta do sandbox era copiado após `closedir`, podendo formar um destino corrompido e causar `PRX_NOT_RESOLVED_FUNCTION`. Agora o nome é copiado antes de fechar o diretório.

**Validação:** compilação com PS5 Payload SDK e `-Wall -Werror`. No log da versão 1.1 após repouso, `PPSA17221` e `PPSA28180` montaram antes do `EXEC` na primeira tentativa e desmontaram com `rc=0`. A primeira abertura desta versão após boot limpo ainda não foi verificada por log acessível. Outros firmwares não foram testados nesta integração. Esta é uma versão experimental.

**Arquivo:** `kstuff-a53-fast-native.elf`

**SHA-256:** `3713495c9c4fe4ff819507e0b9c3490c7694adbe97dbaef8dc363339497da0f5`

**Créditos:** BackPork foi criado por [BestPig](https://github.com/BestPig/BackPork). Esta integração também se baseia em [EchoStretch/kstuff-lite](https://github.com/EchoStretch/kstuff-lite), [drakmor/ppr-patch](https://github.com/drakmor/ppr-patch) e [cragson/a53-code-exec](https://github.com/cragson/a53-code-exec). Detalhes em [`README.md`](README.md) e [`CREDITS.md`](CREDITS.md).
