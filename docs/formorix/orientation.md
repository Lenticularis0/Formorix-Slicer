# Düsenorientierung

<a id="oberflachennormale"></a>
## Oberflächennormale

Die gewünschte Düsenachse zeigt vom TCP nach aussen in Richtung Düse. Standardmässig entspricht sie der geglätteten lokalen Oberflächennormale. Die Orientierung wird an jedem Bahnstützpunkt gespeichert und zwischen Zeitpunkten sphärisch interpoliert. Die Position wird linear interpoliert.

Diese Achse ist eine gewünschte TCP-Pose. Sie enthält keine maschinenspezifischen A-/B-Achswerte, keine inverse Kinematik und keine Kollisionsprüfung.

<a id="abweichung-und-kippwinkel"></a>
## Abweichung und Kippwinkel

Wenn eine Abweichung zugelassen ist, darf die Düse um höchstens den eingestellten Wert von bis zu 15° in Richtung global +Z geneigt werden. Dies geschieht nur, wenn die Oberflächennormale den im Druckerprofil eingestellten Kippwinkel überschreitet.

Eine senkrechte Wand erfordert zusätzlich **Senkrechte Flächen zulassen**. Bei 75° maximalem Kippwinkel und 15° erlaubter Abweichung kann ihre gewünschte Pose dargestellt werden. Ohne beide Voraussetzungen wird sie abgelehnt. Das bestätigt noch keine mechanische Erreichbarkeit: Maschinenkinematik, Kopfgehäuse, Nachbarköpfe und Kollisionsbedingungen sind ungeprüft.
