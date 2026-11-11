# Profilzuordnung und gespeicherte Prozesswerte

Geprüft am 11. November 2026 unter Windows, Release-Konfiguration.

`FormorixProfiles` löst die drei Kopfzuordnungen vor jeder Berechnung aus aktuellen
Orca-Presets auf. Gespeicherte Prozesswerte dienen nur als Momentaufnahme.
Änderungen unter unverändertem Presetnamen werden über die serialisierten
Konfigurationsinhalte erkannt. Die normale Orca-Profilverwaltung bleibt erhalten.

Jeder Kopf besitzt eine eigene Materialzuordnung sowie ein Drucker-Preset als
Düsenquelle und einen expliziten Düsenplatz. Leere Presetnamen folgen den aktuell
bearbeiteten Orca-Presets. Die Bettgeometrie und experimentelle Drucksperre stammen
weiterhin von der aktiven Maschine. Schema 3 speichert die Düsenbindung; Projekte
mit Schema 1 oder 2 werden migriert. Beim Laden werden alte Prozessmomentaufnahmen
vor erneuter Verwendung verworfen.

## Tatsächlich ausgeführte Prüfungen

```powershell
cmake --build build --config Release --target libslic3r_tests fff_print_tests -- /m:2 /nologo /v:minimal
build/tests/libslic3r/Release/libslic3r_tests.exe "[FormorixProfiles],[FormorixSurfaces]" --reporter compact --order rand --rng-seed 42
build/tests/libslic3r/Release/libslic3r_tests.exe "[FilamentVariants]" --reporter compact --order rand --rng-seed 42
```

| Prüfung | Ergebnis |
| --- | --- |
| Beide Release-Testprogramme gebaut | Exitcode 0 |
| Formorix-Profil- und Oberflächenspeicherung | 13 Testfälle, 168 Assertions bestanden |
| Vorhandene Orca-Materialvarianten-Regression | 1 Testfall, 352 Assertions bestanden |

Der Build meldete die bereits vorhandenen Linkerhinweise LNK4098 und `/LTCG`
ohne erforderliche Codegenerierung. In diesem Teilauftrag wurde kein kompletter
GUI-Build oder visueller Abnahmetest ausgeführt; diese Prüfungen werden im
übergeordneten Auftrag dokumentiert.

Die neuen Profiltests prüfen insbesondere:

- Drei separate Material-IDs mit 0,8/0,4/0,6-mm-Düsen, 240/205/250 °C und
  getrennten Volumenfluss-/Flow-Werten. Prozentuale Linienbreiten beziehen sich
  auf die jeweilige Düse. Lokale Oberflächenschichthöhen werden explizit übernommen.
- Einen ausdrücklich ausgewählten dritten Düsenplatz mit eigener Geschwindigkeit.
- Die korrekte High-Flow-Materialvariante, einschließlich 272 °C und 28 mm³/s,
  statt versehentlicher Verwendung des Standard-Slots mit 230 °C und 10 mm³/s.
- Invalidierung nach gleichnamigen Änderungen von Düse, Temperatur, Flow-Grenze,
  Schrumpfkompensation, Linienbreite und Geschwindigkeit.
- Verwerfung manipulierter/geladener Momentaufnahmen, fehlende Presets, ungültige
  Werte sowie negative, gebrochene und überlaufende Düsenindizes.
- Native 3MF-Rundläufe der Kopfzuordnungen und Abwärtsmigration der Projektversionen.

## Grenzen

Ein gemischtes physisches Düseninventar innerhalb einer einzelnen Orca-Extrudernummer
(beispielsweise der Orca-Volumentyp `Hybrid`) ist noch keine eindeutige
Formorix-Kopfbindung. Solche Quellen und fehlende Materialvarianten werden mit einem
Fehler abgewiesen. Drei separate Köpfe mit unterschiedlichen Düsendurchmessern und
Standard-/High-Flow-Profilen sind davon nicht betroffen.

Der aktuelle Orca-Prozess bleibt die gemeinsame Quelle der Prozessvorgaben;
nozzle-relative Werte und vorhandene per-extruder Varianten werden je Kopf aufgelöst.
Die tatsächlichen Kern-Extrusionsbahnen verwenden ihre von Orca berechneten
rollenabhängigen Breiten und Höhen. Die Profiltests sind keine geometrische oder
physikalische 0,01-mm-Genauigkeitsfreigabe.
