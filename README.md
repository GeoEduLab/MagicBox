<p align="center"><img src="logo.png" width="128" alt="MagicBox"></p>

# MagicBox — Cutiuțele Magice

**Versiunea 5.3.1** · aplicație pentru Windows · în română (și în engleză, din Setări)

MagicBox este o cutie cu senzori pentru ora de științe. Elevii măsoară cu ea
temperatura, umiditatea, presiunea aerului, lumina, dioxidul de carbon și
mișcarea (accelerație și rotație). Pe calculator, aplicația MagicBox arată
măsurătorile pe grafice, în timp real, și îi conduce pe elevi prin experimente
ghidate, instrumente de explorare și jocuri, de la clasa a II-a până la a XII-a.

Aici găsiți tot ce vă trebuie pentru clasă: **aplicația**, **ghidurile pentru
profesori**, **fișele pentru elevi** și **firmware-ul cutiei**, toate din
aceeași versiune.

---

## Ce găsiți în folderul `MagicBox`

| Ce | Unde |
|---|---|
| Aplicația — se pornește cu dublu-clic | `MagicBox.exe` |
| Ghidurile pentru profesori, câte unul pe activitate | [docs/ro/ghiduri_profesor](MagicBox/docs/ro/ghiduri_profesor) |
| Fișele pentru elevi (o foaie A4 față-verso) | [docs/ro/fise_elev](MagicBox/docs/ro/fise_elev) |
| Planul curricular: ce activitate la ce clasă și materie | [plan_curricular.pdf](MagicBox/docs/ro/plan_curricular.pdf) |
| Manualul tehnic: senzorii, datele, problemele frecvente | [manual_tehnic.pdf](MagicBox/docs/ro/manual_tehnic.pdf) |
| Firmware-ul cutiei și ghidul de instalare | [firmware](MagicBox/firmware) |
| Ce s-a schimbat de la o versiune la alta | [CHANGELOG.txt](MagicBox/CHANGELOG.txt) |
| Aceleași documente în engleză | [docs/en](MagicBox/docs/en) |

Ghidurile și fișele se pot deschide direct aici, pe GitHub, fără să
descărcați nimic.

---

## Instalarea aplicației (Windows 10 / 11)

1. Apăsați butonul verde **Code → Download ZIP** din partea de sus a paginii.
2. Dezarhivați fișierul ZIP (clic dreapta → *Extract All…*), de exemplu pe
   Desktop sau pe un stick.
3. Deschideți folderul `MagicBox` și porniți **`MagicBox.exe`**.

Nu e nevoie de instalare și nici de Python. Țineți însă **tot folderul
`MagicBox` împreună**: programul folosește fișierele de lângă el, deci
`MagicBox.exe` mutat singur nu pornește.

**Windows poate afișa „Windows protected your PC”** la prima pornire,
pentru că programul nu este semnat digital. Apăsați **More info → Run
anyway**. Dacă Windows întreabă despre firewall, permiteți accesul pe
**rețele private**: așa găsește aplicația cutiile prin WiFi.

**Conectarea cutiei:** prin cablul USB, cutia apare singură în câteva
secunde. Prin WiFi, cutiile din aceeași rețea cu calculatorul se conectează
singure, fără să tastați vreo adresă. Cum puneți o cutie pe rețeaua școlii
scrie în aplicație, la **Setări → Cutiile prin WiFi**.

---

## Instalarea firmware-ului pe cutie

Cutiile care funcționează **nu trebuie reprogramate**. Firmware-ul se încarcă
doar când construiți o cutie nouă, schimbați o placă sau treceți la o
versiune nouă cerută de o activitate. Pașii, cu desene, sunt în
[ghidul de instalare a firmware-ului](MagicBox/firmware/FLASHING_GUIDE_RO.pdf)
(circa 30 de minute prima dată, apoi 2–3 minute pe cutie).

Folderul [firmware](MagicBox/firmware) conține două variante:
`serial_wifi_…` (USB și WiFi, recomandată) și `serial_only_…` (doar USB).
Ce s-a schimbat în firmware scrie în
[firmware/CHANGELOG.txt](MagicBox/firmware/CHANGELOG.txt).

**Senzorul de mișcare arată invers?** Așezați cutia pe masă și priviți
accelerația Z în **Grafice în timp real**: trebuie să fie cam +1 g. Dacă o
cutie arată cam −1 g, bifați **Senzor de mișcare întors** la acea cutie, în
**Setări → Numele cutiilor**. Nu e nevoie de alt firmware.

---

## Activitățile și documentele lor

Fiecare activitate are un ghid pentru profesor (pregătire, desfășurarea orei,
întrebări, ce pot greși elevii) și o fișă pentru elev. Clasele sunt cele din
[planul curricular](MagicBox/docs/ro/plan_curricular.pdf).

### Experimente ghidate

| Activitate | Clasele | Ghidul profesorului | Fișa elevului | English |
|---|---|---|---|---|
| 🌞 Lumina de pretutindeni | II–IV | [ghid](MagicBox/docs/ro/ghiduri_profesor/lumina_de_pretutindeni.pdf) | [fișă](MagicBox/docs/ro/fise_elev/lumina_de_pretutindeni.pdf) | [guide](MagicBox/docs/en/teacher_guides/light_is_everywhere.pdf) · [sheet](MagicBox/docs/en/student_sheets/light_is_everywhere.pdf) |
| 🌡️ Termometrul magic | II–IV | [ghid](MagicBox/docs/ro/ghiduri_profesor/termometrul_magic.pdf) | [fișă](MagicBox/docs/ro/fise_elev/termometrul_magic.pdf) | [guide](MagicBox/docs/en/teacher_guides/magic_thermometer.pdf) · [sheet](MagicBox/docs/en/student_sheets/magic_thermometer.pdf) |
| 🫁 Aerul pe care îl respirăm | II–IV | [ghid](MagicBox/docs/ro/ghiduri_profesor/aerul_pe_care_il_respiram.pdf) | [fișă](MagicBox/docs/ro/fise_elev/aerul_pe_care_il_respiram.pdf) | [guide](MagicBox/docs/en/teacher_guides/air_we_breathe.pdf) · [sheet](MagicBox/docs/en/student_sheets/air_we_breathe.pdf) |
| 🏠 Poluarea de interior | V–VIII | [ghid](MagicBox/docs/ro/ghiduri_profesor/poluarea_de_interior.pdf) | [fișă](MagicBox/docs/ro/fise_elev/poluarea_de_interior.pdf) | [guide](MagicBox/docs/en/teacher_guides/indoor_pollution.pdf) · [sheet](MagicBox/docs/en/student_sheets/indoor_pollution.pdf) |
| 🌿 CO₂ și fotosinteza | X–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/co2_si_fotosinteza.pdf) | [fișă](MagicBox/docs/ro/fise_elev/co2_si_fotosinteza.pdf) | [guide](MagicBox/docs/en/teacher_guides/co2_and_photosynthesis.pdf) · [sheet](MagicBox/docs/en/student_sheets/co2_and_photosynthesis.pdf) |
| 📊 Seismograful | V–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/seismograful.pdf) | [fișă](MagicBox/docs/ro/fise_elev/seismograful.pdf) | [guide](MagicBox/docs/en/teacher_guides/seismometer.pdf) · [sheet](MagicBox/docs/en/student_sheets/seismometer.pdf) |
| ✈️ Zbor drept | II–IV | [ghid](MagicBox/docs/ro/ghiduri_profesor/zbor_drept.pdf) | [fișă](MagicBox/docs/ro/fise_elev/zbor_drept.pdf) | [guide](MagicBox/docs/en/teacher_guides/flying_straight.pdf) · [sheet](MagicBox/docs/en/student_sheets/flying_straight.pdf) |
| ⚖️ Cutia în echilibru | II–IV | [ghid](MagicBox/docs/ro/ghiduri_profesor/cutia_in_echilibru.pdf) | [fișă](MagicBox/docs/ro/fise_elev/cutia_in_echilibru.pdf) | [guide](MagicBox/docs/en/teacher_guides/box_in_balance.pdf) · [sheet](MagicBox/docs/en/student_sheets/box_in_balance.pdf) |
| ⛰️ Presiune și altitudine | V–VIII | [ghid](MagicBox/docs/ro/ghiduri_profesor/presiune_si_altitudine.pdf) | [fișă](MagicBox/docs/ro/fise_elev/presiune_si_altitudine.pdf) | [guide](MagicBox/docs/en/teacher_guides/pressure_and_altitude.pdf) · [sheet](MagicBox/docs/en/student_sheets/pressure_and_altitude.pdf) |
| 🌆 Insule termice | V–VIII | [ghid](MagicBox/docs/ro/ghiduri_profesor/insule_termice.pdf) | [fișă](MagicBox/docs/ro/fise_elev/insule_termice.pdf) | [guide](MagicBox/docs/en/teacher_guides/urban_heat_islands.pdf) · [sheet](MagicBox/docs/en/student_sheets/urban_heat_islands.pdf) |
| ⚡ Panouri solare | IX–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/panouri_solare.pdf) | [fișă](MagicBox/docs/ro/fise_elev/panouri_solare.pdf) | [guide](MagicBox/docs/en/teacher_guides/solar_panels.pdf) · [sheet](MagicBox/docs/en/student_sheets/solar_panels.pdf) |
| 🔥 Convecția aerului | V–VIII | [ghid](MagicBox/docs/ro/ghiduri_profesor/convectia_aerului.pdf) | [fișă](MagicBox/docs/ro/fise_elev/convectia_aerului.pdf) | [guide](MagicBox/docs/en/teacher_guides/air_convection.pdf) · [sheet](MagicBox/docs/en/student_sheets/air_convection.pdf) |
| 💧 Ciclul apei | V–VIII | [ghid](MagicBox/docs/ro/ghiduri_profesor/ciclul_apei.pdf) | [fișă](MagicBox/docs/ro/fise_elev/ciclul_apei.pdf) | [guide](MagicBox/docs/en/teacher_guides/water_cycle.pdf) · [sheet](MagicBox/docs/en/student_sheets/water_cycle.pdf) |
| 🌐 Fizica atmosferei | IX–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/fizica_atmosferei.pdf) | [fișă](MagicBox/docs/ro/fise_elev/fizica_atmosferei.pdf) | [guide](MagicBox/docs/en/teacher_guides/physics_of_the_atmosphere.pdf) · [sheet](MagicBox/docs/en/student_sheets/physics_of_the_atmosphere.pdf) |
| ⚗️ Legea Beer–Lambert | X–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/legea_beer_lambert.pdf) | [fișă](MagicBox/docs/ro/fise_elev/legea_beer_lambert.pdf) | [guide](MagicBox/docs/en/teacher_guides/beer_lambert_law.pdf) · [sheet](MagicBox/docs/en/student_sheets/beer_lambert_law.pdf) |

### Instrumente de explorare

| Activitate | Clasele | Ghidul profesorului | Fișa elevului | English |
|---|---|---|---|---|
| 📌 Carnetul de măsurători | IV–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/carnetul_de_masuratori.pdf) | [fișă](MagicBox/docs/ro/fise_elev/carnetul_de_masuratori.pdf) | [guide](MagicBox/docs/en/teacher_guides/measurement_notebook.pdf) · [sheet](MagicBox/docs/en/student_sheets/measurement_notebook.pdf) |
| 🌦️ Stația meteo | V–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/statia_meteo.pdf) | [fișă](MagicBox/docs/ro/fise_elev/statia_meteo.pdf) | [guide](MagicBox/docs/en/teacher_guides/weather_station.pdf) · [sheet](MagicBox/docs/en/student_sheets/weather_station.pdf) |
| 🌬️ Calitatea aerului | V–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/calitatea_aerului.pdf) | [fișă](MagicBox/docs/ro/fise_elev/calitatea_aerului.pdf) | [guide](MagicBox/docs/en/teacher_guides/air_quality.pdf) · [sheet](MagicBox/docs/en/student_sheets/air_quality.pdf) |
| ☀️ Energia solară | VI–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/energia_solara.pdf) | [fișă](MagicBox/docs/ro/fise_elev/energia_solara.pdf) | [guide](MagicBox/docs/en/teacher_guides/solar_energy.pdf) · [sheet](MagicBox/docs/en/student_sheets/solar_energy.pdf) |
| 📐 Seismologie | V–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/seismologie.pdf) | [fișă](MagicBox/docs/ro/fise_elev/seismologie.pdf) | [guide](MagicBox/docs/en/teacher_guides/seismology.pdf) · [sheet](MagicBox/docs/en/student_sheets/seismology.pdf) |
| 🌆 Insule termice cu mai multe cutii | V–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/insule_termice_cu_mai_multe_cutii.pdf) | [fișă](MagicBox/docs/ro/fise_elev/insule_termice_cu_mai_multe_cutii.pdf) | [guide](MagicBox/docs/en/teacher_guides/heat_islands_with_several_boxes.pdf) · [sheet](MagicBox/docs/en/student_sheets/heat_islands_with_several_boxes.pdf) |
| 🌿 Efectul de seră | V–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/efectul_de_sera.pdf) | [fișă](MagicBox/docs/ro/fise_elev/efectul_de_sera.pdf) | [guide](MagicBox/docs/en/teacher_guides/greenhouse_effect.pdf) · [sheet](MagicBox/docs/en/student_sheets/greenhouse_effect.pdf) |
| 🔬 Spectroscopie | IX–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/spectroscopie.pdf) | [fișă](MagicBox/docs/ro/fise_elev/spectroscopie.pdf) | [guide](MagicBox/docs/en/teacher_guides/spectroscopy.pdf) · [sheet](MagicBox/docs/en/student_sheets/spectroscopy.pdf) |

### Jocuri

| Activitate | Clasele | Ghidul profesorului | Fișa elevului | English |
|---|---|---|---|---|
| 🎮 Printre nori | II–VIII | [ghid](MagicBox/docs/ro/ghiduri_profesor/printre_nori.pdf) | [fișă](MagicBox/docs/ro/fise_elev/printre_nori.pdf) | [guide](MagicBox/docs/en/teacher_guides/among_the_clouds.pdf) · [sheet](MagicBox/docs/en/student_sheets/among_the_clouds.pdf) |
| 🎮 Cutremur! | IV–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/cutremur.pdf) | [fișă](MagicBox/docs/ro/fise_elev/cutremur.pdf) | [guide](MagicBox/docs/en/teacher_guides/earthquake.pdf) · [sheet](MagicBox/docs/en/student_sheets/earthquake.pdf) |
| 🎮 Rezonanța | VI–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/rezonanta.pdf) | [fișă](MagicBox/docs/ro/fise_elev/rezonanta.pdf) | [guide](MagicBox/docs/en/teacher_guides/resonance.pdf) · [sheet](MagicBox/docs/en/student_sheets/resonance.pdf) |
| 🎯 Ținta giroscopică | V–XII | [ghid](MagicBox/docs/ro/ghiduri_profesor/tinta_giroscopica.pdf) | [fișă](MagicBox/docs/ro/fise_elev/tinta_giroscopica.pdf) | [guide](MagicBox/docs/en/teacher_guides/gyro_target.pdf) · [sheet](MagicBox/docs/en/student_sheets/gyro_target.pdf) |

---

## English

MagicBox is a sensor box for science lessons (temperature, humidity, air
pressure, light, CO₂, motion) with a Windows application that charts the
measurements live and guides pupils aged 7–19 through experiments, tools and
games. **Code → Download ZIP**, unzip, keep the `MagicBox` folder together and
run `MagicBox.exe`; switch the language in **Settings**. Teacher guides and
pupil sheets are in [docs/en](MagicBox/docs/en); the firmware and its
[flashing guide](MagicBox/firmware/FLASHING_GUIDE.pdf) are in
[firmware](MagicBox/firmware).
