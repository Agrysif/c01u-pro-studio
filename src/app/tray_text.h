#pragma once
// Tray menu texts. The web UI has the full translations; the tray only needs these few.
#include <string>

namespace app {

struct TrayText {
  const char* lang;
  const wchar_t* open;
  const wchar_t* mute;
  const wchar_t* unmute;
  const wchar_t* processing;
  const wchar_t* monitor;
  const wchar_t* exit;
  const wchar_t* muted;
  const wchar_t* active;
  const wchar_t* preview;
  const wchar_t* removed;
};

static const TrayText kTrayTexts[] = {
    {"en", L"Open control panel", L"Mute microphone", L"Unmute microphone", L"Sound processing", L"Listen to myself",
     L"Exit", L"Microphone muted", L"Driver active", L"Preview mode (driver not active)", L"The driver has been removed."},
    {"ru", L"Открыть панель", L"Выключить микрофон", L"Включить микрофон", L"Обработка звука", L"Слушать себя", L"Выход",
     L"Микрофон выключен", L"Драйвер активен", L"Режим предпросмотра (драйвер не активен)", L"Драйвер удалён."},
    {"uk", L"Відкрити панель", L"Вимкнути мікрофон", L"Увімкнути мікрофон", L"Обробка звуку", L"Слухати себе", L"Вихід",
     L"Мікрофон вимкнено", L"Драйвер активний", L"Режим попереднього перегляду (драйвер не активний)", L"Драйвер видалено."},
    {"be", L"Адкрыць панэль", L"Выключыць мікрафон", L"Уключыць мікрафон", L"Апрацоўка гуку", L"Слухаць сябе", L"Выхад",
     L"Мікрафон выключаны", L"Драйвер актыўны", L"Рэжым папярэдняга прагляду (драйвер не актыўны)", L"Драйвер выдалены."},
    {"kk", L"Панельді ашу", L"Микрофонды өшіру", L"Микрофонды қосу", L"Дыбысты өңдеу", L"Өзімді тыңдау", L"Шығу",
     L"Микрофон өшірулі", L"Драйвер белсенді", L"Алдын ала қарау режимі (драйвер белсенді емес)", L"Драйвер жойылды."},
    {"uz", L"Panelni ochish", L"Mikrofonni o‘chirish", L"Mikrofonni yoqish", L"Ovozni qayta ishlash", L"O‘zimni eshitish",
     L"Chiqish", L"Mikrofon o‘chirilgan", L"Drayver faol", L"Oldindan ko‘rish rejimi (drayver faol emas)",
     L"Drayver o‘chirildi."},
    {"ky", L"Панелди ачуу", L"Микрофонду өчүрүү", L"Микрофонду күйгүзүү", L"Үндү иштетүү", L"Өзүмдү угуу", L"Чыгуу",
     L"Микрофон өчүк", L"Драйвер активдүү", L"Алдын ала көрүү режими (драйвер активдүү эмес)", L"Драйвер өчүрүлдү."},
    {"tg", L"Кушодани панел", L"Хомӯш кардани микрофон", L"Фаъол кардани микрофон", L"Коркарди садо", L"Худамро шунидан",
     L"Баромад", L"Микрофон хомӯш аст", L"Драйвер фаъол аст", L"Реҷаи пешнамоиш (драйвер фаъол нест)", L"Драйвер нест карда шуд."},
    {"hy", L"Բացել վահանակը", L"Անջատել խոսափողը", L"Միացնել խոսափողը", L"Ձայնի մշակում", L"Լսել ինձ", L"Ելք",
     L"Խոսափողն անջատված է", L"Դրայվերն ակտիվ է", L"Նախադիտման ռեժիմ (դրայվերն ակտիվ չէ)", L"Դրայվերը հեռացված է։"},
    {"az", L"Paneli aç", L"Mikrofonu söndür", L"Mikrofonu yandır", L"Səs emalı", L"Özümü dinlə", L"Çıxış",
     L"Mikrofon söndürülüb", L"Drayver aktivdir", L"Önizləmə rejimi (drayver aktiv deyil)", L"Drayver silindi."},
    {"ka", L"პანელის გახსნა", L"მიკროფონის გამორთვა", L"მიკროფონის ჩართვა", L"ხმის დამუშავება", L"საკუთარი თავის მოსმენა",
     L"გასვლა", L"მიკროფონი გამორთულია", L"დრაივერი აქტიურია", L"გადახედვის რეჟიმი (დრაივერი არ არის აქტიური)",
     L"დრაივერი წაიშალა."},
    {"ro", L"Deschide panoul", L"Dezactivează microfonul", L"Activează microfonul", L"Procesare sunet", L"Ascultă-mă",
     L"Ieșire", L"Microfon dezactivat", L"Driver activ", L"Mod previzualizare (driverul nu este activ)", L"Driverul a fost eliminat."},
    {"tk", L"Paneli aç", L"Mikrofony öçür", L"Mikrofony aç", L"Sesi işlemek", L"Özümi diňle", L"Çykyş", L"Mikrofon öçürilen",
     L"Draýwer işjeň", L"Öňünden görmek tertibi (draýwer işjeň däl)", L"Draýwer aýryldy."},
    {"de", L"Steuerung öffnen", L"Mikrofon stummschalten", L"Mikrofon einschalten", L"Klangbearbeitung",
     L"Mich selbst hören", L"Beenden", L"Mikrofon stumm", L"Treiber aktiv", L"Vorschaumodus (Treiber nicht aktiv)",
     L"Der Treiber wurde entfernt."},
    {"fr", L"Ouvrir le panneau", L"Couper le micro", L"Réactiver le micro", L"Traitement du son", L"M'écouter", L"Quitter",
     L"Micro coupé", L"Pilote actif", L"Mode aperçu (pilote inactif)", L"Le pilote a été supprimé."},
    {"es", L"Abrir panel", L"Silenciar micrófono", L"Activar micrófono", L"Procesamiento de sonido", L"Escucharme",
     L"Salir", L"Micrófono silenciado", L"Controlador activo", L"Modo vista previa (controlador inactivo)",
     L"El controlador se ha eliminado."},
    {"it", L"Apri pannello", L"Disattiva microfono", L"Attiva microfono", L"Elaborazione audio", L"Ascoltami", L"Esci",
     L"Microfono disattivato", L"Driver attivo", L"Modalità anteprima (driver non attivo)", L"Il driver è stato rimosso."},
    {"pt", L"Abrir painel", L"Silenciar microfone", L"Ativar microfone", L"Processamento de som", L"Ouvir-me", L"Sair",
     L"Microfone silenciado", L"Driver ativo", L"Modo de pré-visualização (driver inativo)", L"O driver foi removido."},
    {"pl", L"Otwórz panel", L"Wycisz mikrofon", L"Włącz mikrofon", L"Przetwarzanie dźwięku", L"Słuchaj siebie",
     L"Zakończ", L"Mikrofon wyciszony", L"Sterownik aktywny", L"Tryb podglądu (sterownik nieaktywny)",
     L"Sterownik został usunięty."},
    {"tr", L"Paneli aç", L"Mikrofonu kapat", L"Mikrofonu aç", L"Ses işleme", L"Kendimi dinle", L"Çıkış", L"Mikrofon kapalı",
     L"Sürücü etkin", L"Önizleme modu (sürücü etkin değil)", L"Sürücü kaldırıldı."},
    {"zh", L"打开控制面板", L"静音麦克风", L"取消麦克风静音", L"声音处理", L"监听自己", L"退出", L"麦克风已静音",
     L"驱动已启用", L"预览模式（驱动未启用）", L"驱动已卸载。"},
    {"ja", L"パネルを開く", L"マイクをミュート", L"マイクのミュートを解除", L"音声処理", L"自分の声を聞く", L"終了",
     L"マイクはミュート中", L"ドライバー有効", L"プレビューモード（ドライバー無効）", L"ドライバーを削除しました。"},
    {"ko", L"패널 열기", L"마이크 음소거", L"마이크 음소거 해제", L"사운드 처리", L"내 목소리 듣기", L"종료",
     L"마이크 음소거됨", L"드라이버 활성", L"미리보기 모드(드라이버 비활성)", L"드라이버가 제거되었습니다."},
    {"ar", L"فتح لوحة التحكم", L"كتم الميكروفون", L"إلغاء كتم الميكروفون", L"معالجة الصوت", L"الاستماع إلى نفسي",
     L"خروج", L"الميكروفون مكتوم", L"برنامج التشغيل نشط", L"وضع المعاينة (برنامج التشغيل غير نشط)",
     L"تمت إزالة برنامج التشغيل."},
    {"hi", L"पैनल खोलें", L"माइक्रोफ़ोन म्यूट करें", L"माइक्रोफ़ोन अनम्यूट करें", L"ध्वनि प्रसंस्करण", L"खुद को सुनें",
     L"बाहर निकलें", L"माइक्रोफ़ोन म्यूट है", L"ड्राइवर सक्रिय", L"पूर्वावलोकन मोड (ड्राइवर सक्रिय नहीं)", L"ड्राइवर हटा दिया गया।"},
};

inline const TrayText& TrayTextFor(const std::string& lang) {
  for (const TrayText& t : kTrayTexts)
    if (lang == t.lang) return t;
  return kTrayTexts[0];
}

}  // namespace app
