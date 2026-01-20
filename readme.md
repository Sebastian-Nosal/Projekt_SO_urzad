Podział logiki:

Kolejki Komunikatów
 - biletomat_in - input do biletomatu
 - biletomat_out - output z biletomatu
 - petent_{PID} - komunikaty do petenta o danym PID 
 - SA, SC, KM, ML, PD - kolejki do komunikacji z urzędnikami
 - monitoring - kolejka do logów do plików txt i std::out
 - loader_in - informacje do loadera

Pamięć współdzielona
 - ilosc_aktywnych_urzednikow - ilu urzędników pracuje (pisarz loader, czytelnicy: dyrektor, biletomat)
 - statusy urzędników
 - ilosc_petentow_w_budynku - ilu petentów w środku (pisarze: loader, urzędnik, czytelnicy - loader, biletomat)
 - urzad_otwarty -bool czy urząd jest jeszcze czynny

Sygnały
 - Sygnał 1: Urzędnik kończy pracę po obsłużeniu bieżącego petenta.
 - Sygnał 2: Wszyscy petenci natychmiast opuszczają budynek.

Procesy:

main: 
 - uruchamia symulację, 
 - pobiera dane konfiguracyjne,
 - pobiera i waliduje dane wejściowe ze standardowego wejścia
 - przechwytuje SIGINT i SIGQUIT i wywołuje dyrektora do zamknięcia urzędu

loader:
 - ładuje procesy,
 - zarządza ilością petentów w budynku
 - generuje nowych petentów
 - zarządza ilością biletomatów 

monitoring:
 - zapisuje logi:
  - kiedy powstał proces petenta
  - kiedy wszedł do budynku
  - kiedy pobrał bilet i do którego wydziału (jaka była kolejka)
  - kiedy został wezwany przez urzędnika
  - jak został obsłużony
    - czy od razu rozwiązano sprawę
    - czy został przekierowany do innego urzędnika
    - czy odesłano do kasy
    - czy odprawiono z powodu wyczerpania urzędnika
  - kiedy wyszedł
  - czy był z dzieckiem.
 - zapewnia wyraźne logi debugu i działania
 - kolorowa składnia [log], [error], [debug]

petent:
 - przy każdej podejmowanej akcji wysyła komunikat do procesu monitoring
 - odpytuje loader czy moze wejść do budynku
 - odpytuje biletomat o bilet do wybranego urzędu (powstały przy stworzneiu procesu)
 - czeka na wezwanie od urzędnika
 - po wezwaniu reaguje na komendy urzędnika (idź do kasy, idź do innego urzędnika, odejdź jestem wyczerpany)
 - po wyjściu z urzędu informują loader o opuszczeniu obszaru urzędu
 - jeżeli dostają sygnał informuja loader o wyjściu, po czym piszą komuniakt o frustracji i przez 2 minuty czekają, po czym się kończą
 
urzednik:
 - przyjmuje z kolejki komunikatów pid następnego procesu petenta do obsłużenia
 - zaprasza petenta do siebie
 - losowo odsyła od innego urzędnika / obsługuje / wysyła do kasy
 - po obsłużeniu określonej w configu liczby petentów wyczerpuje się - wszystkich następnych petentów odprawia i pisze to do biletomatu,zmniejsza licznik urzędników shm

biletomat:
 - na komuniakt od petenta dodaje jego pid do kolejki, incrementuje. Sprawdza czy PID jest VIPem
 - wysyła komuniakt przez mq do urzędnika. VIP ma priorytet.
 - przy komunikacie od urzędnika o przekierowaniu do innego okienka, wydaje bilet z wysokim priorytetem
 - jeżeli wszyscy urzędnicy danej kategorii się wyczerpali, automatycznie odprawia następnych petentów, nie wydaje biletów
 - przy zamknięciu urzędu wysyła do petentów sygnał o zakończeniu pracy 

dyrektor:
 - wysyła SIGUSR1 do urzednika aby ten natychmiast sie wyczerpał
 - wysyła SIGUSR2 do petentów aby natychmiast ich odprawić
 - wysyła SIGINT/SIGQUIT aby grzecznie zakończyć procesy urzedników przy końcu symulacji

 kasa:
  - odbiera komunikat z kolejki i w odpowiedzi odsyła petenta do urzędnika który wysłał petenta do kasy, z biletem o priorytecie wysokim




