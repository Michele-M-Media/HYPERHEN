# ELF testado no firmware 5.00

O upstream publicou `kstuff-a53-fast-klog-experimental.elf` como o ELF testado
no console PS5 FW 5.00 durante o desenvolvimento da integração kstuff +
A53/PPR. HyperHEN não redistribui nem usa esse binário: o core é sempre
compilado do código-fonte vendorizado.

- SHA-256 upstream: `9d0cadca3664d861b1e7d23fef22ca6d9e5e20b4dc634e6a21b91238dbe68686`
- Fonte correspondente: tag [`fw5-tested-source`](https://github.com/Soonniicc/kstuff-a53/tree/fw5-tested-source), baseada no commit `2506a5b15af501734f7ea3dce2759c077cdb76c6` do kstuff-lite.
- A branch `main` atual inclui também a integração BackPork; este binário antigo corresponde à tag indicada e não contém essa integração.
- A compilação local de uma árvore equivalente terminou sem erros, mas o ELF gerado teve hash diferente. Por isso, não afirmamos que qualquer build da fonte seja idêntico byte a byte a este arquivo testado.
- Não há BackPork neste ELF.

Este artefato é experimental; o teste informado refere-se ao firmware 5.00. Outros firmwares precisam de validação própria.
