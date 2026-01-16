# Testy integracyjne

Te testy są napisane jako **bash** i zakładają środowisko POSIX (np. **WSL** albo **Git Bash/MSYS2**). Na Windows PowerShell bez warstwy POSIX one nie zadziałają, bo projekt używa `shm_open`, semaforów POSIX i ścieżek `/tmp`.

## Wymagania

- `bash`
- `g++`
- (opcjonalnie) `stdbuf` (z `coreutils`) – ułatwia czytanie logów “na żywo”

## Uruchomienie

1. Zbuduj binarki:

```bash
./build_workers.sh
```

2. Uruchom testy:

```bash
chmod +x tests/run_tests.sh
bash tests/run_tests.sh
```

Logi testów zapisują się do `tests/out/`.

## Co jest testowane

1. Czy biletomat wydaje bilet (pojawia się log o przydziale / petent widzi bilet).
2. Czy `main` ogranicza liczbę petentów do softcap (test używa `--dry-run`/`SIM_DRY_RUN=1`, żeby nie odpalać symulacji i nie tworzyć procesów).
3. Czy petent potrafi “czekać” na bilet, gdy biletomat jeszcze nie działa, i dostaje go po starcie biletomatu.
4. Czy po sygnale zamknięcia (symulacja sygnału dyrektora) procesy kończą pracę.
