# 5D-Vorschau und Zeitleiste

**5D-Vorschau berechnen** verarbeitet den aktiven markierten Bereich und öffnet den vorhandenen Tab **Vorschau**. Die horizontale Zeitleiste läuft von null bis zum Ende aller vorhandenen Kopfspuren. Sie ist zeitbasiert, nicht schichtbasiert.

**Hybrid-Vorschau berechnen** verarbeitet dagegen alle markierten Bereiche der gewählten Instanz und ergänzt tatsächliche Orca-Kernbahnen. [Bedienung, Anzeigen und Prüfwerte der Hybrid-Vorschau](hybrid.md).

| Bedienung | Wirkung |
| --- | --- |
| Start / Pause | Zeitabhängige Wiedergabe starten oder anhalten. |
| `\|<` | Zum Anfang zurückkehren. |
| −1 s / +1 s | In Zeitschritten vor- oder zurückspringen. |
| 0,25× / 1× / 4× / 16× | Wiedergabegeschwindigkeit. |
| Zeitschieber | Alle Kopfposen auf die gewählte Zeit setzen; Wiedergabe pausiert. |
| Lagenwahl | Eine einzelne Lage oder alle Lagen anzeigen. Die Uhr bleibt unverändert. |
| 5D-Schale / Verbleibender Kern | In der einfachen 5D-Vorschau Näherungsflächen, in der Hybrid-Vorschau die tatsächlich berechneten Volumenkörper einblenden. |
| Orca-Vorschau | Experimentellen Cache verwerfen und zur normalen Orca-Vorschau wechseln. |

Goldene Linien sind zukünftige Bahnen, grüne Linien bereits abgearbeitete Bahnen. Weiss hebt den aktuellen Extrusionsabschnitt hervor; in der Hybrid-Vorschau erscheint ein aktiver Verfahrabschnitt orange. Kleine Pfeile zeigen die Richtung, grüne und rote Punkte Anfang und Ende. Düsenspitzen und kurze Achsen erscheinen nur für Köpfe mit einer aus ihrer Bewegungsspur bekannten Vorschaupose: grün arbeitet, orange fährt, grau wartet. Vor dem ersten Abschnitt wartet der Kopf an dessen Anfang, während einer Lücke und nach dem letzten Abschnitt am zuletzt berechneten Endpunkt. Das sind Vorschaupositionen, keine gemessenen Maschinenpositionen.

Die Statuszeile führt weiterhin alle drei Köpfe auf. Ein Kopf ohne Bewegungsspur hat keine bekannte Position; für ihn werden keine erfundene Düsenspitze, Achse oder Koordinaten angezeigt. Nicht berechenbare ausgewählte Flächen erscheinen rot mit einer kurzen Fehlermeldung.

Der Prüfstatus des [nominalen Materialvolumens](material.md#nominales-materialvolumen) bezieht sich auf sämtliche berechneten Lagen. Zeit- und Lagenregler verändern die Darstellung, nicht diesen geometrischen Gesamtnachweis.

Die geschätzte Dauer verwendet Bahnlänge geteilt durch Profilgeschwindigkeit, einschliesslich der Verbindungen zwischen Lagen. Beschleunigungen, thermische Wartezeiten, Unterbau, Stützen und normales Kern-Slicing sind nicht enthalten. Die gesamte angezeigte Zeit umfasst den berechneten 5D-Vorgang, nicht einen vollständigen fertigen Maschinenauftrag.

Die Uhr trägt deshalb die Kennzeichnung **5D**. Der Hinweis **„Zeit: nur 5D-Teil. Orca-Kernbahnen sind nicht synchronisiert; Supports und Wartezeiten fehlen.“** bleibt Teil der Zeitleiste. Auf kleinen Fenstern lässt sich der Bedienbereich senkrecht scrollen.

Lagenwechsel und lokale Verbindungen zwischen beendeten Extrusionsabschnitten sind ungeprüfte Verfahrbewegungen. Sie dürfen nicht als kollisionsfrei verstanden werden. Die Zeitleiste zeigt dauerhaft **„Verfahrbewegungen beim Lagenwechsel: Kollisionsprüfung ausstehend.“** und ergänzt den Zustand **Fährt** um denselben Prüfstatus. Es findet weiterhin keine Kollisionsberechnung statt.

Die Wiedergabe verändert weder Modell noch Projektparameter. Beim Wechsel des Tabs pausiert sie. Nach Modell-, Auswahl-, Profil- oder Parametereingriffen wird ein veraltetes Ergebnis verworfen. Ein gespeichertes 3MF-Projekt enthält Markierungen und Einstellungen; beim erneuten Öffnen **5D-Vorschau berechnen** wählen.
