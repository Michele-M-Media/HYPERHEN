#include "hyper_locale.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(HYPER_LOCALE_SHELLUI)
#include "external_symbols.hpp"
#elif defined(HYPER_P5_PS5) || defined(__SCE__) || defined(PS5)
extern "C" int sceSystemServiceParamGetInt(int, int *);
#endif

namespace {
// System IDs match HyperHEN's onion_notify_resolve_language(0, system_id).
constexpr HyperLocale profiles[] = {
    {0,"ja-JP","ja","ja","日本語"},
    {1,"en-US","en","en-us","English (United States)"},
    {2,"fr-FR","fr","fr-fr","français"},
    {3,"es-ES","es","es","español (España)"},
    {4,"de-DE","de","de","Deutsch"},
    {5,"it-IT","it","it","italiano"},
    {6,"nl-NL","nl","nl","Nederlands"},
    {7,"pt-PT","pt","pt","português (Portugal)"},
    {8,"ru-RU","ru","ru","русский"},
    {9,"ko-KR","ko","ko","한국어"},
    {10,"zh-Hant","zh","cmn","繁體中文"},
    {11,"zh-Hans","zh","cmn","简体中文"},
    {12,"fi-FI","fi","fi","suomi"},
    {13,"sv-SE","sv","sv","svenska"},
    {14,"da-DK","da","da","dansk"},
    {15,"no-NO","no","nb","norsk"},
    {16,"pl-PL","pl","pl","polski"},
    {17,"pt-BR","pt","pt-br","português (Brasil)"},
    {18,"en-GB","en","en-gb","English (United Kingdom)"},
    {19,"tr-TR","tr","tr","Türkçe"},
    {20,"es-419","es","es-419","español (Latinoamérica)"},
    {21,"ar-SA","ar","ar","العربية"},
    {22,"fr-CA","fr","fr-fr","français (Canada)"},
    {23,"cs-CZ","cs","cs","čeština"},
    {24,"hu-HU","hu","hu","magyar"},
    {25,"el-GR","el","el","ελληνικά"},
    {26,"ro-RO","ro","ro","română"},
    {27,"th-TH","th","th","ไทย"},
    {28,"vi-VN","vi","vi","tiếng Việt"},
    {29,"id-ID","id","id","Bahasa Indonesia"},
    {30,"uk-UA","uk","uk","українська"}
};
constexpr unsigned catalog_for_id[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,1,18,3,19,2,20,21,22,23,24,25,26,27};
struct Catalog { const char *messages[10]; const char *fast[3]; const char *aliases[3]; };
constexpr Catalog catalogs[] = {
    { // Japanese
     {"Hyperの準備ができました。L1 + R1 + □で呼び出し、○でキャンセルします。","もうすぐ聞き始めます。○でキャンセルできます。","今話してください。最大12秒間話せます。間を置くと文が終了します。","聞こえました。少し考えています。","終わりました。同じボタンの組み合わせでまた呼んでください。","中断しました。もう一度ボタンで呼んでください。","本体を完全に再起動してください。","Hyperのローカルファイルがありません。モデルと音声データをインストールしてください。","ローカルモデル用のメモリが足りません。","Hyperはすでにバックグラウンドで動いています。"},
     {"いますよ！Hyperです。耳もジョークも準備できています！","Hyperです。このPlayStationに住んでいます。気軽に話しかけてください！","呼び出したり、名前を聞いたり、自由に会話できます。○でキャンセルできます。"},
     {"ハイパーいる|ハイパー いる|ハイパーいますか|hyper いる|いる","名前は|名前は何|あなたの名前は|なんていう名前","助けて|ヘルプ|使い方"}},
    { // English
     {"Hyper is ready. L1 + R1 + Square calls me; Circle cancels.","I'll listen in a moment. Circle cancels.","Speak now. You have up to twelve seconds; a pause ends the sentence.","I heard you. Give me a moment to think.","Finished. Call me again with the same combination.","The session was interrupted. Try the combination again.","A full console restart is required.","Local Hyper files are missing. Install the model and voice data.","Not enough memory for the local models.","Hyper is already running in the background."},
     {"I'm here! Hyper reporting for duty, ears open and jokes almost ready.","I'm Hyper. I live in your PlayStation and I'm here for a chat!","Call me, ask my name, or chat freely. Circle cancels."},
     {"hyper are you there|are you there hyper|are you there","what is your name|what are you called|hyper what is your name","help|hyper help"}},
    { // French
     {"Hyper est prêt. L1 + R1 + Carré pour m'appeler ; Rond pour annuler.","Je vais écouter dans un instant. Rond pour annuler.","Parlez maintenant. Vous avez jusqu’à douze secondes ; une pause termine la phrase.","Je t'ai entendu. Je réfléchis un instant.","Terminé. Rappelle-moi avec la même combinaison.","La session a été interrompue. Essaie à nouveau la combinaison.","Un redémarrage complet de la console est nécessaire.","Les fichiers locaux de Hyper manquent. Installe le modèle et les données vocales.","Mémoire insuffisante pour les modèles locaux.","Hyper fonctionne déjà en arrière-plan."},
     {"Je suis là ! Hyper au rapport, les oreilles ouvertes et les blagues presque prêtes.","Je m'appelle Hyper. J'habite dans ta PlayStation et je suis là pour discuter !","Appelle-moi, demande mon nom ou parle librement. Rond pour annuler."},
     {"hyper tu es là|tu es là hyper|tu es là|hyper es tu là","comment tu t'appelles|comment t'appelles tu|quel est ton nom","aide|aide moi|hyper aide"}},
    { // Spanish
     {"Hyper está listo. L1 + R1 + Cuadrado para llamarme; Círculo para cancelar.","Voy a escucharte en un momento. Círculo para cancelar.","Habla ahora. Tienes hasta doce segundos; una pausa termina la frase.","Te he oído. Dame un momento para pensar.","Terminado. Llámame otra vez con la misma combinación.","La sesión se ha interrumpido. Prueba otra vez la combinación.","Es necesario reiniciar completamente la consola.","Faltan archivos locales de Hyper. Instala el modelo y los datos de voz.","No hay memoria suficiente para los modelos locales.","Hyper ya se está ejecutando en segundo plano."},
     {"¡Aquí estoy! Hyper presente, con los oídos abiertos y los chistes casi listos.","Me llamo Hyper. Vivo en tu PlayStation y estoy aquí para charlar.","Llámame, pregunta mi nombre o conversa libremente. Círculo para cancelar."},
     {"hyper estás ahí|estás ahí hyper|estás ahí|hyper estas ahi","cómo te llamas|como te llamas|cuál es tu nombre","ayuda|hyper ayuda|ayúdame"}},
    { // German
     {"Hyper ist bereit. L1 + R1 + Quadrat ruft mich; Kreis bricht ab.","Ich höre gleich zu. Kreis bricht ab.","Sprich jetzt. Du hast bis zu zwölf Sekunden; eine Pause beendet den Satz.","Ich habe dich gehört. Ich denke kurz nach.","Fertig. Ruf mich mit derselben Tastenkombination wieder.","Die Sitzung wurde unterbrochen. Versuche die Tastenkombination erneut.","Die Konsole muss vollständig neu gestartet werden.","Lokale Hyper-Dateien fehlen. Installiere das Modell und die Sprachdaten.","Nicht genug Speicher für die lokalen Modelle.","Hyper läuft bereits im Hintergrund."},
     {"Ich bin da! Hyper meldet sich, mit offenen Ohren und fast fertigen Witzen.","Ich heiße Hyper. Ich wohne in deiner PlayStation und bin zum Plaudern da!","Ruf mich, frag nach meinem Namen oder unterhalte dich frei. Kreis bricht ab."},
     {"hyper bist du da|bist du da hyper|bist du da","wie heißt du|wie heisst du|wie ist dein name","hilfe|hyper hilfe|hilf mir"}},
    { // Italian
     {"Hyper è pronto. L1 + R1 + Quadrato per chiamarmi; Cerchio annulla.","Ti ascolto tra poco. Cerchio annulla.","Parla ora. Hai fino a dodici secondi; una pausa conclude la frase.","Ho ascoltato. Un attimo, sto pensando.","Ho finito. Puoi richiamarmi con la stessa combinazione.","La sessione è stata interrotta. Riprova con la combinazione.","È necessario un riavvio completo della console.","Mancano i file locali di Hyper. Installa il modello e i dati della voce.","Memoria insufficiente per i modelli locali.","Hyper è già in esecuzione in background."},
     {"Ci sono! Hyper presente, orecchie aperte e battute quasi sempre pronte.","Mi chiamo Hyper. Abito nella tua PlayStation e sono qui per fare due chiacchiere!","Eccomi! Puoi chiamarmi, chiedermi come mi chiamo oppure parlarmi liberamente. Cerchio annulla."},
     {"hyper ci sei|ci sei hyper|ci sei","come ti chiami|hyper come ti chiami","aiuto|hyper aiuto"}},
    { // Dutch
     {"Hyper is klaar. Roep me met L1 + R1 + Vierkant; Cirkel annuleert.","Ik luister zo. Cirkel annuleert.","Spreek nu. Je hebt maximaal twaalf seconden; een pauze beëindigt de zin.","Ik heb je gehoord. Ik denk even na.","Klaar. Roep me opnieuw met dezelfde toetsencombinatie.","De sessie is onderbroken. Probeer de toetsencombinatie opnieuw.","De console moet volledig opnieuw worden opgestart.","Lokale Hyper-bestanden ontbreken. Installeer het model en de spraakgegevens.","Onvoldoende geheugen voor de lokale modellen.","Hyper draait al op de achtergrond."},
     {"Ik ben er! Hyper meldt zich, met open oren en bijna kant-en-klare grappen.","Ik heet Hyper. Ik woon in je PlayStation en ben er om te praten!","Roep me, vraag mijn naam of praat vrijuit. Cirkel annuleert."},
     {"hyper ben je daar|ben je daar hyper|ben je daar","hoe heet je|wat is je naam","help|hyper help|help me"}},
    { // Portuguese, Portugal
     {"O Hyper está pronto. L1 + R1 + Quadrado para me chamar; Círculo cancela.","Vou ouvir-te daqui a pouco. Círculo cancela.","Fale agora. Tem até doze segundos; uma pausa termina a frase.","Já te ouvi. Vou pensar um instante.","Terminei. Chama-me novamente com a mesma combinação.","A sessão foi interrompida. Experimenta novamente a combinação.","É necessário reiniciar completamente a consola.","Faltam ficheiros locais do Hyper. Instala o modelo e os dados de voz.","Memória insuficiente para os modelos locais.","O Hyper já está a funcionar em segundo plano."},
     {"Estou aqui! Hyper presente, de ouvidos atentos e piadas quase prontas.","Chamo-me Hyper. Vivo na tua PlayStation e estou aqui para conversar!","Chama-me, pergunta o meu nome ou conversa à vontade. Círculo cancela."},
     {"hyper estás aí|estás aí hyper|estás aí|hyper estas ai","como te chamas|qual é o teu nome|qual e o teu nome","ajuda|hyper ajuda|ajuda me"}},
    { // Russian
     {"Hyper готов. Вызов: L1 + R1 + Квадрат; отмена: Круг.","Скоро начну слушать. Круг отменяет сеанс.","Говорите сейчас. У вас до двенадцати секунд; пауза завершает фразу.","Я тебя услышал. Дай немного подумать.","Готово. Снова вызови меня той же комбинацией.","Сеанс прерван. Попробуй вызвать меня снова.","Необходимо полностью перезапустить консоль.","Отсутствуют локальные файлы Hyper. Установи модель и голосовые данные.","Недостаточно памяти для локальных моделей.","Hyper уже работает в фоновом режиме."},
     {"Я здесь! Hyper на связи: уши открыты, шутки почти готовы.","Меня зовут Hyper. Я живу в твоей PlayStation и готов поболтать!","Позови меня, спроси имя или просто поговори. Круг отменяет сеанс."},
     {"hyper ты здесь|хайпер ты здесь|ты здесь|hyper ты тут|ты тут","как тебя зовут|как твоё имя|как твое имя","помощь|помоги|хайпер помоги"}},
    { // Korean
     {"Hyper가 준비됐어요. L1 + R1 + 네모로 부르고 동그라미로 취소하세요.","곧 듣기 시작할게요. 동그라미로 취소할 수 있어요.","지금 말하세요. 최대 12초 동안 말할 수 있으며, 잠시 멈추면 문장이 끝납니다.","잘 들었어요. 잠깐 생각할게요.","끝났어요. 같은 버튼 조합으로 다시 불러 주세요.","세션이 중단됐어요. 버튼 조합으로 다시 시도해 주세요.","콘솔을 완전히 다시 시작해야 해요.","Hyper의 로컬 파일이 없어요. 모델과 음성 데이터를 설치해 주세요.","로컬 모델을 위한 메모리가 부족해요.","Hyper가 이미 백그라운드에서 실행 중이에요."},
     {"여기 있어요! Hyper 대기 중이에요. 귀도 열려 있고 농담도 거의 준비됐어요.","제 이름은 Hyper예요. 이 PlayStation에 살면서 대화할 준비를 하고 있어요!","저를 부르거나 이름을 묻거나 자유롭게 이야기하세요. 동그라미로 취소해요."},
     {"하이퍼 있어|하이퍼 있니|hyper 있어|거기 있어","이름이 뭐야|네 이름은|이름이 뭐니","도와줘|도움말|하이퍼 도와줘"}},
    { // Traditional Chinese
     {"Hyper已就緒。按L1 + R1 + 方形呼叫，圓圈取消。","我很快就開始聆聽。按圓圈可取消。","現在請說話。最長十二秒，停頓即可結束句子。","我聽到了。讓我想一下。","完成了。可用相同按鍵組合再次呼叫我。","對話已中斷。請再試一次按鍵組合。","需要完整重新啟動主機。","缺少Hyper的本機檔案。請安裝模型和語音資料。","記憶體不足，無法使用本機模型。","Hyper已在背景執行。"},
     {"我在！Hyper隨時待命，耳朵打開了，笑話也差不多準備好了。","我叫Hyper，住在你的PlayStation裡，隨時可以聊天！","你可以呼叫我、問我的名字，或自由聊天。按圓圈可取消。"},
     {"hyper 你在嗎|你在嗎|你在嗎 hyper|hyper 在嗎|在嗎","你叫什麼名字|你叫什麼|你的名字是什麼","幫忙|幫幫我|說明|hyper 幫忙"}},
    { // Simplified Chinese
     {"Hyper已就绪。按L1 + R1 + 方形呼叫，圆圈取消。","我很快就开始聆听。按圆圈可取消。","现在请说话。最长十二秒，停顿即可结束句子。","我听到了。让我想一下。","完成了。可以用相同按键组合再次呼叫我。","对话已中断。请再试一次按键组合。","需要完整重启主机。","缺少Hyper的本地文件。请安装模型和语音数据。","内存不足，无法使用本地模型。","Hyper已在后台运行。"},
     {"我在！Hyper随时待命，耳朵打开了，笑话也差不多准备好了。","我叫Hyper，住在你的PlayStation里，随时可以聊天！","你可以呼叫我、问我的名字，或自由聊天。按圆圈可取消。"},
     {"hyper 你在吗|你在吗|你在吗 hyper|hyper 在吗|在吗","你叫什么名字|你叫什么|你的名字是什么","帮忙|帮帮我|帮助|hyper 帮忙"}},
    { // Finnish
     {"Hyper on valmis. Kutsu painamalla L1 + R1 + Neliö; Ympyrä peruuttaa.","Aloitan kuuntelun pian. Ympyrä peruuttaa.","Puhu nyt. Sinulla on enintään kaksitoista sekuntia; tauko päättää lauseen.","Kuulin sinut. Mietin hetken.","Valmista. Kutsu minua uudelleen samalla yhdistelmällä.","Istunto keskeytyi. Kokeile näppäinyhdistelmää uudelleen.","Konsoli on käynnistettävä kokonaan uudelleen.","Hyperin paikallisia tiedostoja puuttuu. Asenna malli ja puhetiedot.","Muisti ei riitä paikallisille malleille.","Hyper on jo käynnissä taustalla."},
     {"Täällä ollaan! Hyper valmiina, korvat auki ja vitsit melkein valmiina.","Nimeni on Hyper. Asun PlayStationissasi ja olen valmis juttelemaan!","Kutsu minua, kysy nimeäni tai juttele vapaasti. Ympyrä peruuttaa."},
     {"hyper oletko siellä|oletko siellä hyper|oletko siellä","mikä sinun nimesi on|mikä nimesi on","apua|hyper apua|auta minua"}},
    { // Swedish
     {"Hyper är redo. L1 + R1 + Fyrkant kallar på mig; Cirkel avbryter.","Jag börjar lyssna strax. Cirkel avbryter.","Tala nu. Du har upp till tolv sekunder; en paus avslutar meningen.","Jag hörde dig. Jag tänker en stund.","Klart. Kalla på mig igen med samma knappkombination.","Sessionen avbröts. Prova knappkombinationen igen.","Konsolen måste startas om helt.","Lokala Hyper-filer saknas. Installera modellen och röstdata.","Det finns inte tillräckligt med minne för de lokala modellerna.","Hyper körs redan i bakgrunden."},
     {"Jag är här! Hyper på plats, med öppna öron och nästan färdiga skämt.","Jag heter Hyper. Jag bor i din PlayStation och är här för att prata!","Kalla på mig, fråga vad jag heter eller prata fritt. Cirkel avbryter."},
     {"hyper är du där|är du där hyper|är du där","vad heter du|vad är ditt namn","hjälp|hyper hjälp|hjälp mig"}},
    { // Danish
     {"Hyper er klar. L1 + R1 + Firkant kalder på mig; Cirkel annullerer.","Jeg begynder snart at lytte. Cirkel annullerer.","Tal nu. Du har op til tolv sekunder; en pause afslutter sætningen.","Jeg hørte dig. Jeg tænker et øjeblik.","Færdig. Kald på mig igen med samme knapkombination.","Sessionen blev afbrudt. Prøv knapkombinationen igen.","Konsollen skal genstartes helt.","Lokale Hyper-filer mangler. Installer modellen og stemmedata.","Der er ikke nok hukommelse til de lokale modeller.","Hyper kører allerede i baggrunden."},
     {"Jeg er her! Hyper er klar med åbne ører og næsten færdige vittigheder.","Jeg hedder Hyper. Jeg bor i din PlayStation og er klar til en snak!","Kald på mig, spørg om mit navn eller tal frit. Cirkel annullerer."},
     {"hyper er du der|er du der hyper|er du der","hvad hedder du|hvad er dit navn","hjælp|hyper hjælp|hjælp mig"}},
    { // Norwegian
     {"Hyper er klar. L1 + R1 + Firkant kaller på meg; Sirkel avbryter.","Jeg begynner å lytte snart. Sirkel avbryter.","Snakk nå. Du har opptil tolv sekunder; en pause avslutter setningen.","Jeg hørte deg. Jeg tenker litt.","Ferdig. Kall på meg igjen med samme tastekombinasjon.","Økten ble avbrutt. Prøv tastekombinasjonen igjen.","Konsollen må startes helt på nytt.","Lokale Hyper-filer mangler. Installer modellen og stemmedataene.","Det er ikke nok minne til de lokale modellene.","Hyper kjører allerede i bakgrunnen."},
     {"Jeg er her! Hyper er på plass, med åpne ører og nesten ferdige vitser.","Jeg heter Hyper. Jeg bor i PlayStationen din og er klar for en prat!","Kall på meg, spør om navnet mitt eller snakk fritt. Sirkel avbryter."},
     {"hyper er du der|er du der hyper|er du der","hva heter du|hva er navnet ditt","hjelp|hyper hjelp|hjelp meg"}},
    { // Polish
     {"Hyper jest gotowy. Wywołaj mnie: L1 + R1 + Kwadrat; Kółko anuluje.","Za chwilę zacznę słuchać. Kółko anuluje.","Mów teraz. Masz do dwunastu sekund; pauza kończy zdanie.","Usłyszałem cię. Daj mi chwilę do namysłu.","Gotowe. Wywołaj mnie ponownie tą samą kombinacją.","Sesja została przerwana. Spróbuj ponownie tej kombinacji.","Wymagane jest pełne ponowne uruchomienie konsoli.","Brakuje lokalnych plików Hyper. Zainstaluj model i dane głosu.","Za mało pamięci dla lokalnych modeli.","Hyper już działa w tle."},
     {"Jestem! Hyper na posterunku, uszy otwarte, żarty prawie gotowe.","Nazywam się Hyper. Mieszkam w twojej PlayStation i chętnie pogadam!","Wywołaj mnie, zapytaj o imię lub porozmawiaj swobodnie. Kółko anuluje."},
     {"hyper jesteś tam|jesteś tam hyper|jesteś tam","jak się nazywasz|jak masz na imię","pomoc|hyper pomoc|pomóż mi"}},
    { // Portuguese, Brazil
     {"O Hyper está pronto. L1 + R1 + Quadrado para me chamar; Círculo cancela.","Vou ouvir você daqui a pouco. Círculo cancela.","Fale agora. Você tem até doze segundos; uma pausa encerra a frase.","Ouvi você. Vou pensar um instante.","Terminei. Pode me chamar de novo com a mesma combinação.","A sessão foi interrompida. Tente a combinação novamente.","É necessário reiniciar o console completamente.","Faltam arquivos locais do Hyper. Instale o modelo e os dados de voz.","Memória insuficiente para os modelos locais.","O Hyper já está funcionando em segundo plano."},
     {"Estou aqui! Hyper na área, ouvidos atentos e piadas quase prontas.","Meu nome é Hyper. Moro no seu PlayStation e estou aqui para conversar!","Pode me chamar, perguntar meu nome ou conversar à vontade. Círculo cancela."},
     {"hyper você está aí|você está aí hyper|você está aí|hyper voce esta ai","qual é o seu nome|como você se chama|como voce se chama","ajuda|hyper ajuda|me ajuda"}},
    { // Turkish
     {"Hyper hazır. L1 + R1 + Kare ile çağır; Daire iptal eder.","Birazdan dinleyeceğim. Daire iptal eder.","Şimdi konuşun. En fazla on iki saniyeniz var; duraklama cümleyi bitirir.","Seni duydum. Biraz düşüneyim.","Bitti. Aynı tuş birleşimiyle beni tekrar çağırabilirsin.","Oturum kesildi. Tuş birleşimini tekrar dene.","Konsolun tamamen yeniden başlatılması gerekiyor.","Hyper'ın yerel dosyaları eksik. Modeli ve ses verilerini yükle.","Yerel modeller için yeterli bellek yok.","Hyper zaten arka planda çalışıyor."},
     {"Buradayım! Hyper görev başında, kulaklar açık ve espriler neredeyse hazır.","Benim adım Hyper. PlayStation'ında yaşıyorum, sohbet etmeye hazırım!","Beni çağır, adımı sor veya serbestçe sohbet et. Daire iptal eder."},
     {"hyper orada mısın|orada mısın hyper|orada mısın","adın ne|senin adın ne|ismin ne","yardım|hyper yardım|yardım et"}},
    { // Arabic
     {"Hyper جاهز. استدعني عبر L1 + R1 + مربع؛ الدائرة للإلغاء.","سأبدأ الاستماع بعد قليل. الدائرة للإلغاء.","تحدّث الآن. لديك حتى اثنتي عشرة ثانية؛ التوقف المؤقت ينهي الجملة.","سمعتك. دعني أفكر قليلًا.","انتهيت. استدعني مجددًا بنفس مجموعة الأزرار.","انقطعت الجلسة. جرّب مجموعة الأزرار مجددًا.","يجب إعادة تشغيل الجهاز بالكامل.","ملفات Hyper المحلية غير موجودة. ثبّت النموذج وبيانات الصوت.","لا توجد ذاكرة كافية للنماذج المحلية.","Hyper يعمل بالفعل في الخلفية."},
     {"أنا هنا! Hyper حاضر، أذناي مفتوحتان والنكات شبه جاهزة.","اسمي Hyper. أعيش داخل جهاز PlayStation الخاص بك وأنا مستعد للدردشة!","نادني أو اسألني عن اسمي أو تحدث بحرية. الدائرة للإلغاء."},
     {"hyper هل أنت هنا|هل أنت هنا|هل انت هنا|هايبر هل أنت هنا","ما اسمك|ما هو اسمك","مساعدة|ساعدني|هايبر مساعدة"}},
    { // Czech
     {"Hyper je připraven. Zavolej mě pomocí L1 + R1 + Čtverec; Kruh ruší.","Za chvíli začnu poslouchat. Kruh ruší.","Mluvte teď. Máte až dvanáct sekund; pauza ukončí větu.","Slyšel jsem tě. Chvilku přemýšlím.","Hotovo. Zavolej mě znovu stejnou kombinací.","Relace byla přerušena. Zkus kombinaci znovu.","Je nutný úplný restart konzole.","Chybí místní soubory Hyper. Nainstaluj model a hlasová data.","Pro místní modely není dost paměti.","Hyper už běží na pozadí."},
     {"Jsem tady! Hyper hlásí připravenost, uši otevřené a vtipy skoro hotové.","Jmenuji se Hyper. Bydlím ve tvé PlayStation a rád si popovídám!","Zavolej mě, zeptej se na jméno nebo si volně povídej. Kruh ruší."},
     {"hyper jsi tam|jsi tam hyper|jsi tam","jak se jmenuješ|jaké je tvoje jméno","pomoc|hyper pomoc|pomoz mi"}},
    { // Hungarian
     {"Hyper készen áll. L1 + R1 + Négyzet hív, Kör megszakít.","Mindjárt figyelek. A Kör megszakítja.","Most beszélj. Legfeljebb tizenkét másodperced van; a szünet lezárja a mondatot.","Hallottalak. Egy pillanatig gondolkodom.","Kész. Hívj újra ugyanazzal a gombkombinációval.","A munkamenet megszakadt. Próbáld újra a gombkombinációt.","A konzolt teljesen újra kell indítani.","Hiányoznak a Hyper helyi fájljai. Telepítsd a modellt és a hangadatokat.","Nincs elég memória a helyi modellekhez.","A Hyper már fut a háttérben."},
     {"Itt vagyok! Hyper jelentkezik, nyitott fülekkel és majdnem kész viccekkel.","Hyper a nevem. A PlayStationödben lakom, és szívesen beszélgetek!","Hívj, kérdezd meg a nevem, vagy beszélgess szabadon. A Kör megszakítja."},
     {"hyper itt vagy|itt vagy hyper|itt vagy","hogy hívnak|mi a neved","segítség|hyper segítség|segíts"}},
    { // Greek
     {"Ο Hyper είναι έτοιμος. L1 + R1 + Τετράγωνο για κλήση, Κύκλος για ακύρωση.","Θα αρχίσω να ακούω σε λίγο. Ο Κύκλος ακυρώνει.","Μιλήστε τώρα. Έχετε έως δώδεκα δευτερόλεπτα· μια παύση ολοκληρώνει την πρόταση.","Σε άκουσα. Ας σκεφτώ λίγο.","Τελείωσα. Κάλεσέ με ξανά με τον ίδιο συνδυασμό.","Η συνεδρία διακόπηκε. Δοκίμασε ξανά τον συνδυασμό.","Χρειάζεται πλήρης επανεκκίνηση της κονσόλας.","Λείπουν τοπικά αρχεία του Hyper. Εγκατάστησε το μοντέλο και τα δεδομένα φωνής.","Δεν υπάρχει αρκετή μνήμη για τα τοπικά μοντέλα.","Ο Hyper εκτελείται ήδη στο παρασκήνιο."},
     {"Εδώ είμαι! Ο Hyper παρών, με ανοιχτά αυτιά και σχεδόν έτοιμα αστεία.","Με λένε Hyper. Ζω στο PlayStation σου και είμαι έτοιμος για κουβέντα!","Κάλεσέ με, ρώτησε το όνομά μου ή μίλησε ελεύθερα. Ο Κύκλος ακυρώνει."},
     {"hyper είσαι εκεί|είσαι εκεί hyper|είσαι εκεί","πώς σε λένε|πως σε λενε|ποιο είναι το όνομά σου","βοήθεια|hyper βοήθεια|βοήθησέ με"}},
    { // Romanian
     {"Hyper este gata. L1 + R1 + Pătrat mă cheamă; Cerc anulează.","Voi începe să ascult în curând. Cerc anulează.","Vorbește acum. Ai până la douăsprezece secunde; o pauză încheie propoziția.","Te-am auzit. Mă gândesc puțin.","Am terminat. Cheamă-mă din nou cu aceeași combinație.","Sesiunea a fost întreruptă. Încearcă din nou combinația.","Consola trebuie repornită complet.","Lipsesc fișiere locale Hyper. Instalează modelul și datele vocale.","Memorie insuficientă pentru modelele locale.","Hyper rulează deja în fundal."},
     {"Sunt aici! Hyper la datorie, cu urechile deschise și glumele aproape gata.","Mă numesc Hyper. Locuiesc în PlayStationul tău și sunt gata să povestim!","Cheamă-mă, întreabă-mi numele sau vorbește liber. Cerc anulează."},
     {"hyper ești acolo|ești acolo hyper|ești acolo","cum te cheamă|care este numele tău","ajutor|hyper ajutor|ajută mă"}},
    { // Thai
     {"Hyper พร้อมแล้ว กด L1 + R1 + สี่เหลี่ยมเพื่อเรียก และวงกลมเพื่อยกเลิก","อีกสักครู่ฉันจะเริ่มฟัง กดวงกลมเพื่อยกเลิก","พูดได้เลย มีเวลาสูงสุดสิบสองวินาที การหยุดพูดจะจบประโยค","ได้ยินแล้ว ขอคิดสักครู่","เสร็จแล้ว เรียกฉันอีกครั้งด้วยปุ่มชุดเดิมได้เลย","เซสชันถูกขัดจังหวะ ลองกดปุ่มชุดเดิมอีกครั้ง","ต้องรีสตาร์ตคอนโซลใหม่ทั้งหมด","ไม่มีไฟล์ในเครื่องของ Hyper โปรดติดตั้งโมเดลและข้อมูลเสียง","หน่วยความจำไม่พอสำหรับโมเดลในเครื่อง","Hyper ทำงานอยู่เบื้องหลังแล้ว"},
     {"อยู่นี่แล้ว! Hyper พร้อมฟัง ส่วนมุกตลกก็เกือบพร้อมแล้ว","ฉันชื่อ Hyper อาศัยอยู่ใน PlayStation ของคุณและพร้อมคุยด้วยเสมอ!","เรียกฉัน ถามชื่อ หรือคุยได้อย่างอิสระ กดวงกลมเพื่อยกเลิก"},
     {"hyper อยู่ไหม|ไฮเปอร์อยู่ไหม|อยู่ไหม","ชื่ออะไร|คุณชื่ออะไร","ช่วยด้วย|ช่วยหน่อย|วิธีใช้"}},
    { // Vietnamese
     {"Hyper đã sẵn sàng. Nhấn L1 + R1 + Vuông để gọi; Tròn để hủy.","Tôi sẽ bắt đầu nghe ngay. Nhấn Tròn để hủy.","Hãy nói ngay. Bạn có tối đa mười hai giây; ngừng nói sẽ kết thúc câu.","Tôi nghe rồi. Cho tôi suy nghĩ một chút.","Xong rồi. Hãy gọi lại bằng cùng tổ hợp nút.","Phiên đã bị gián đoạn. Hãy thử lại tổ hợp nút.","Cần khởi động lại hoàn toàn máy chơi game.","Thiếu tệp Hyper cục bộ. Hãy cài mô hình và dữ liệu giọng nói.","Không đủ bộ nhớ cho các mô hình cục bộ.","Hyper đã chạy ở chế độ nền."},
     {"Tôi đây! Hyper đã có mặt, tai mở và chuyện cười gần sẵn sàng.","Tôi tên là Hyper. Tôi sống trong PlayStation của bạn và luôn sẵn sàng trò chuyện!","Gọi tôi, hỏi tên hoặc trò chuyện tự do. Nhấn Tròn để hủy."},
     {"hyper bạn có đó không|bạn có đó không|hyper có đó không","bạn tên gì|tên bạn là gì","giúp tôi|trợ giúp|hyper trợ giúp"}},
    { // Indonesian
     {"Hyper siap. Panggil dengan L1 + R1 + Kotak; Lingkaran membatalkan.","Sebentar lagi saya mendengarkan. Lingkaran membatalkan.","Bicaralah sekarang. Waktu Anda hingga dua belas detik; jeda mengakhiri kalimat.","Saya sudah mendengar. Beri saya waktu untuk berpikir.","Selesai. Panggil lagi dengan kombinasi yang sama.","Sesi terputus. Coba lagi kombinasi tombolnya.","Konsol harus dimulai ulang sepenuhnya.","Berkas lokal Hyper tidak ada. Instal model dan data suara.","Memori tidak cukup untuk model lokal.","Hyper sudah berjalan di latar belakang."},
     {"Saya di sini! Hyper siap, telinga terbuka dan lelucon hampir siap.","Nama saya Hyper. Saya tinggal di PlayStation kamu dan siap mengobrol!","Panggil saya, tanyakan nama saya, atau mengobrol bebas. Lingkaran membatalkan."},
     {"hyper kamu di sana|kamu di sana|hyper ada","siapa namamu|siapa nama kamu|nama kamu siapa","bantuan|tolong|hyper bantuan"}},
    { // Ukrainian
     {"Hyper готовий. Виклик: L1 + R1 + Квадрат; Коло скасовує.","Скоро почну слухати. Коло скасовує сеанс.","Говоріть зараз. У вас до дванадцяти секунд; пауза завершує речення.","Я тебе почув. Дай трохи подумати.","Готово. Знову виклич мене тією самою комбінацією.","Сеанс перервано. Спробуй комбінацію ще раз.","Потрібно повністю перезапустити консоль.","Відсутні локальні файли Hyper. Встанови модель і голосові дані.","Недостатньо пам'яті для локальних моделей.","Hyper уже працює у фоновому режимі."},
     {"Я тут! Hyper на зв'язку: вуха відкриті, жарти майже готові.","Мене звати Hyper. Я живу у твоїй PlayStation і готовий поговорити!","Поклич мене, запитай ім'я або просто поговори. Коло скасовує сеанс."},
     {"hyper ти тут|хайпер ти тут|ти тут","як тебе звати|яке твоє ім'я","допомога|допоможи|хайпер допоможи"}}
};
static_assert(sizeof(profiles) / sizeof(profiles[0]) == 31);
static_assert(sizeof(catalogs) / sizeof(catalogs[0]) == 28);

unsigned selected(int system_id) { return catalog_for_id[hyper_locale_for_system(system_id).system_id]; }
bool separator(std::uint32_t cp) {
    return cp <= 0x20 || cp == 0x7f || cp == '.' || cp == ',' || cp == '!' || cp == '?' ||
           cp == ':' || cp == ';' || cp == '-' || cp == '\'' || cp == '"' ||
           cp == 0x2019 || cp == 0x3000 || cp == 0x3001 || cp == 0x3002 ||
           cp == 0xff01 || cp == 0xff0c || cp == 0xff1f || cp == 0x60c || cp == 0x61f;
}
void append(std::string &text, std::uint32_t cp) {
    if (cp < 0x80) text += static_cast<char>(cp);
    else if (cp < 0x800) { text += static_cast<char>(0xc0 | cp >> 6); text += static_cast<char>(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { text += static_cast<char>(0xe0 | cp >> 12); text += static_cast<char>(0x80 | ((cp >> 6) & 63)); text += static_cast<char>(0x80 | (cp & 63)); }
    else { text += static_cast<char>(0xf0 | cp >> 18); text += static_cast<char>(0x80 | ((cp >> 12) & 63)); text += static_cast<char>(0x80 | ((cp >> 6) & 63)); text += static_cast<char>(0x80 | (cp & 63)); }
}
std::uint32_t lower(std::uint32_t cp) {
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 0xc0 && cp <= 0xd6) || (cp >= 0xd8 && cp <= 0xde)) return cp + 32;
    if (cp == 0x130 || cp == 0x131) return 'i';
    if (cp == 0x178) return 0xff;
    if (cp == 0x1e9e) return 0xdf;
    if ((cp >= 0x100 && cp <= 0x12f && !(cp & 1)) || (cp >= 0x132 && cp <= 0x137 && !(cp & 1)) ||
        (cp >= 0x14a && cp <= 0x177 && !(cp & 1)) || (cp >= 0x218 && cp <= 0x21b && !(cp & 1)) ||
        (cp >= 0x1e00 && cp <= 0x1eff && !(cp & 1))) return cp + 1;
    if ((cp >= 0x139 && cp <= 0x147 && (cp & 1)) || (cp >= 0x179 && cp <= 0x17d && (cp & 1))) return cp + 1;
    if (cp == 0x1af || cp == 0x1a0) return cp + 1;
    if (cp >= 0x410 && cp <= 0x42f) return cp + 32;
    if (cp >= 0x400 && cp <= 0x40f) return cp + 80;
    if ((cp >= 0x391 && cp <= 0x3a1) || (cp >= 0x3a3 && cp <= 0x3ab)) return cp + 32;
    if (cp == 0x386) return 0x3ac;
    if (cp >= 0x388 && cp <= 0x38a) return cp + 37;
    if (cp == 0x38c) return 0x3cc;
    if (cp == 0x38e || cp == 0x38f) return cp + 63;
    if (cp == 0x3c2) return 0x3c3;
    return cp;
}
std::string normalize(const char *value) {
    std::string result;
    if (!value) return result;
    const auto *cursor = reinterpret_cast<const unsigned char *>(value);
    bool spaced = true;
    while (*cursor) {
        std::uint32_t cp = *cursor++;
        std::uint32_t minimum = 0;
        unsigned extra = 0;
        if (cp >= 0xc2 && cp <= 0xdf) { cp &= 31; extra = 1; minimum = 0x80; }
        else if (cp >= 0xe0 && cp <= 0xef) { cp &= 15; extra = 2; minimum = 0x800; }
        else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; extra = 3; minimum = 0x10000; }
        else if (cp >= 0x80) return {};
        for (unsigned n = 0; n < extra; ++n) {
            if ((*cursor & 0xc0) != 0x80) return {};
            cp = (cp << 6) | (*cursor++ & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return {};
        if (separator(cp)) { if (!spaced) result += ' '; spaced = true; }
        else { append(result, lower(cp)); spaced = false; }
    }
    if (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}
bool alias(const std::string &text, const char *list) {
    const char *begin = list;
    for (;;) {
        const char *end = std::strchr(begin, '|');
        const std::string item = end ? std::string(begin, static_cast<std::size_t>(end - begin)) : std::string(begin);
        if (text == normalize(item.c_str())) return true;
        if (!end) return false;
        begin = end + 1;
    }
}
#if defined(HYPER_LOCALE_TESTING) && !defined(HYPER_P5_PS5) && !defined(__SCE__) && !defined(PS5)
int test_language = 1;
#endif
} // namespace

const HyperLocale &hyper_locale_for_system(int system_id) {
    return profiles[system_id >= 0 && system_id <= 30 ? system_id : 1];
}

int hyper_current_system_language() {
    int language = 1;
#if defined(HYPER_LOCALE_SHELLUI)
    if (!sceSystemServiceParamGetInt || sceSystemServiceParamGetInt(1, &language) < 0 || language < 0 || language > 30) language = 1;
#elif defined(HYPER_P5_PS5) || defined(__SCE__) || defined(PS5)
    if (sceSystemServiceParamGetInt(1, &language) < 0 || language < 0 || language > 30) language = 1;
#elif defined(HYPER_LOCALE_TESTING)
    language = test_language;
#endif
    return hyper_locale_for_system(language).system_id;
}

const char *hyper_message(HyperMessage message, int system_id) {
    const auto index = static_cast<unsigned>(message);
    if (index >= 10) return "";
    return catalogs[selected(system_id == -1 ? hyper_current_system_language() : system_id)].messages[index];
}

const char *hyper_fast_reply(const char *text, int system_id) {
    const auto normalized = normalize(text);
    if (normalized.empty()) return nullptr;
    const auto &catalog = catalogs[selected(system_id == -1 ? hyper_current_system_language() : system_id)];
    for (unsigned i = 0; i < 3; ++i)
        if (alias(normalized, catalog.aliases[i]) || alias(normalized, catalogs[1].aliases[i])) return catalog.fast[i];
    return nullptr;
}

#if defined(HYPER_LOCALE_TESTING) && !defined(HYPER_P5_PS5) && !defined(__SCE__) && !defined(PS5)
void hyper_locale_set_test_language(int system_id) { test_language = hyper_locale_for_system(system_id).system_id; }
#endif
