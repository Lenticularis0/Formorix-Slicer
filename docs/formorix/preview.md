# 5D-Vorschau und Zeitleiste

**5D-Vorschau berechnen** verarbeitet den aktiven markierten Bereich und öffnet den vorhandenen Tab **Vorschau**. Die horizontale Zeitleiste läuft von null bis zum Ende aller vorhandenen Kopfspuren. Sie ist zeitbasiert, nicht schichtbasiert.

| Bedienung | Wirkung |
| --- | --- |
| Start / Pause | Zeitabhängige Wiedergabe starten oder anhalten. |
| `\|<` | Zum Anfang zurückkehren. |
| −1 s / +1 s | In Zeitschritten vor- oder zurückspringen. |
| 0,25× / 1× / 4× / 16× | Wiedergabegeschwindigkeit. |
| Zeitschieber | Alle Kopfposen auf die gewählte Zeit setzen; Wiedergabe pausiert. |
| Lagenwahl | Eine einzelne Lage oder alle Lagen anzeigen. Die Uhr bleibt unverändert. |
| 5D-Schale / Verbleibender Kern | Näherungsflächen einblenden. |
| Orca-Vorschau | Experimentellen Cache verwerfen und zur normalen Orca-Vorschau wechseln. |

Goldene Linien sind zukünftige Bahnen, grüne Linien bereits abgearbeitete Bahnen. Weiss hebt den aktuellen Abschnitt hervor. Kleine Pfeile zeigen die Richtung, grüne und rote Punkte Anfang und Ende. Düsenspitzen und kurze Achsen zeigen die drei Kopfposen: grün arbeitet, orange fährt, grau wartet beziehungsweise ohne Aufgabe. Die Statuszeile unterscheidet die Zustände ausdrücklich. Nicht berechenbare ausgewählte Flächen erscheinen rot mit einer kurzen Fehlermeldung.

Die geschätzte Dauer verwendet Bahnlänge geteilt durch Profilgeschwindigkeit, einschliesslich der Verbindungen zwischen Lagen. Beschleunigungen, thermische Wartezeiten, Unterbau, Stützen und normales Kern-Slicing sind nicht enthalten. Die gesamte angezeigte Zeit umfasst den berechneten 5D-Vorgang, nicht einen vollständigen fertigen Maschinenauftrag.

Die Uhr trägt deshalb die Kennzeichnung **5D**. Direkt darunter steht dauerhaft **„Nur 5D-Teil: ohne normalen Bauteilaufbau, Supports und Wartezeiten.“**

Lagenwechsel sind gerade, ungeprüfte Verbindungen. Sie dürfen nicht als kollisionsfrei verstanden werden. Die Zeitleiste zeigt dauerhaft **„Verfahrbewegungen beim Lagenwechsel: Kollisionsprüfung ausstehend.“** und ergänzt den Zustand **Fährt** um denselben Prüfstatus. Es findet weiterhin keine Kollisionsberechnung statt.

Die Wiedergabe verändert weder Modell noch Projektparameter. Beim Wechsel des Tabs pausiert sie. Nach Modell-, Auswahl-, Profil- oder Parametereingriffen wird ein veraltetes Ergebnis verworfen. Ein gespeichertes 3MF-Projekt enthält Markierungen und Einstellungen; beim erneuten Öffnen **5D-Vorschau berechnen** wählen.
