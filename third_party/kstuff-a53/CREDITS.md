# Créditos e procedência

Este repositório é uma derivação de [EchoStretch/kstuff-lite](https://github.com/EchoStretch/kstuff-lite) e conserva o histórico do projeto original. A base utilizada para a integração foi o commit `2506a5b15af501734f7ea3dce2759c077cdb76c6`. O documento técnico original está em [`docs/KSTUFF_UPSTREAM.md`](docs/KSTUFF_UPSTREAM.md).

A implementação PPR/A53 em `ps5-kstuff-ldr/src/ppr/` foi adaptada com base em [drakmor/ppr-patch](https://github.com/drakmor/ppr-patch), commit `fd4c8224563130e9698d3b2b2f44712826ceb525`. O projeto de origem publica sua licença GNU GPL-3.0. As adaptações locais incluem a chamada a partir do loader, transporte, verificação e medições de inicialização.

[BestPig/BackPork](https://github.com/BestPig/BackPork) forneceu a base do monitor de processos e da montagem `app0/fakelib` em `common/lib`. A adaptação em `ps5-kstuff-ldr/src/backpork.c` mantém o crédito e o aviso GPL-3.0 no arquivo. A integração adiciona execução no mesmo loader, rearmamento após repouso e desmontagem acompanhada por PID.

[cragson/a53-code-exec](https://github.com/cragson/a53-code-exec) foi consultado como referência técnica sobre A53. Este projeto não incorpora o ELF do PoC original. O [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) é uma dependência externa de compilação.

Os submódulos `BearSSL`, `libtomcrypt` e `isa-l_crypto` conservam suas próprias autorias e condições de licença. Não remova os avisos de copyright e licença já presentes nos arquivos. O arquivo [`LICENSE`](LICENSE) contém o texto da GPLv3 aplicável aos componentes distribuídos sob essa licença; ele não altera as licenças específicas de componentes de terceiros.
