## Kirjeldus
See on gümnaasiumiastme praktilise töö "Ajaloolise kirjelduse põhjal Arduino UNO elektroonikaplatvormil loodud väike eesti-inglise-saksa-vene tõlkesõnastik" raames valminud projekti iseseisvaks sooritamiseks vajalike failide varamu. Siin asub Arduinole vajalik kood, korpuse ja nuppude 3D-printimiseks vajalikud STL-failid, joonised, tabelid, juhised jne. _Varamu on valmimisel._

![image](https://github.com/ranno-v/V-ike-eesti-inglise-saksa-vene-t-lkes-nastik/assets/116004672/e272ad2d-063d-4795-a215-66d72dfe9037)

## Komponendid
Seadme ehitamiseks vajalikud komponendid on Arduino Uno plaat, 16-segmendiline LCD-moodul DM8BA10, perforeeritud prototüüpplaat mõõtmetega vähemalt 45 x 20 auku (iga augu vahe 2.54 mm), 10 pistikutega juhet pikkusega vähemalt 20 cm (või 20 pistikuga juhet pikkusega vähemalt 10 cm, mille teises otsas on isoleerita traat), 44 mikrolülitit, 9-voldine patarei koos pesaga, 5,5 mm x 2,1 mm alalisvoolupistik (nn _barrel jack_), vasktraat, jootetina, 4 väikest kruvi, kiirliim, lüliti ja PLA-filament. Vajalikud tööriistad on 3D-printer, jootekolb, arvuti, USB-A/USB-B kaabel, akutrell ja lõikeriist, millega saab lõigata läbi prototüüpplaadi plastiku. Veel on vaja viisi seadmele klahvide märkimiseks, selleks sobib näiteks püsimarker või kleepepaber ja printer.

## Juhised
Esmalt lõigake 7 juhet pooleks ja puhastage lõigatud otsad isoleermaterjalist. Seejärel jootke mikrolülitid ja vasktraat prototüüpplaadi külge nii, nagu on näidatud joonistel klahvistiku_skeem.png ja klahvistiku_foto.png. Skeemil on vasktraat kujutatud punaste joontena ja jootetina roheliste joontena, lülitid pruunide ristkülikutena. Jootke juhtmete lõigatud otsad nummerdatud punaste ringidega märgitud kontaktide külge nii, et juhtmed väljuksid poolelt, kus pole kontakte. Numbritega on märgitud, millisesse Arduiino siini tuleb juhtme pistik panna. Lõigake perforeeritud plaadist ristkülik nii, et selle mõõtmed oleksid 45 x 20 auku (see ristkülik on skeemil märgitud punktiirjoonega). Asetage juhtmed skeemil märgitud siinidesse.

Lõigake pooleks veel 2 juhet ja puhastage lõigatud otsad isoleermaterjalist. Jootke juhtmed LCD-ekraani klemmide V+, G, D, WR ja CS külge ja asetage pistikud vastavalt siinidesse 5V, GND, A2, A1 ja A0.

Printige antud STL-failid välja. Kinnitage "klambrid" superliimiga plaadi külge nii, nagu on näidatud alloleval joonisel. Kinnitage LCD-ekraan superliimiga korpuse kaane külge. Puurige 2 mm-se puuriteraga läbi vertikaalsete osade ning kaane vastavate punktide augud nii, et mikrolülitid oleks kaanes olevate avadega kohakuti. Asetage klahvid avadesse ja kinnitage klambrid kruvidega kaane külge. Jootke 9V patarei alles jäänud juhtmetega läbi lüliti alalisvoolupistiku külge, tehke kaane sisse lüliti jaoks sobiv ava ja kinnitage lüliti kaane külge. Märgistage klahvid, selleks on üks võimalus printida välja fail labels.pdf, lõigata sildid mööda jooni nelja ribana välja ja liimida need kaane külge vastavate klahvide kohale. 

![image](https://github.com/ranno-v/V-ike-eesti-inglise-saksa-vene-t-lkes-nastik/assets/116004672/e51e3a76-94d8-4c2b-8215-bc55c44e7bbe)

