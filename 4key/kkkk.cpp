#define _CRT_SECURE_NO_WARNINGS
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <algorithm>   
#include <cstdio>     

using namespace std;

// 音符判定等级
enum Judge
{
    Intact,
    Fractured,
    Lost
};

// 游戏运行状态机
enum GameState
{
    State_SelectSong,
    State_Playing,
    State_Result
};

// 谱面难度枚举
enum class Difficulty
{
    Ember,
    Rift,
    Shatter,
    Void,
    Count
};

// 音符类型枚举
enum NoteType
{
    NormalNote = 0, // 单点短音符
    HoldNote = 1,   // 长按长条音符
    SlideNote = 2,  // 滑动音符
    Mine = 3        // 地雷音符
};

//  数据存储结构体区域 =
// 歌曲信息结构体：存储单首歌曲路径、封面、BGM、多难度谱面
struct SongInfo
{
    std::string folderPath;
    std::string coverPath;
    std::string bgmPath;
    std::map<Difficulty, std::string> chartPaths;
    std::string title;
};

// 音符数据结构体：存储单个音符全部属性
struct Note
{
    int lane;
    float spawnTime;
    float posY;
    bool deleteFlag;
    NoteType type;
    bool isJudged;
    bool isSlideHit;    // 新增：滑条防重复计分标记
    float holdDuration;
    bool holdStarted;
    float holdPassTime;
    Judge startJudge;
    float releaseBufferTimer;

    float slideLeftX_raw;
    float slideRightX_raw;
};

//  全局常量定义区域
// 轨道与音符基础尺寸常量
const float NoteW = 100.f;
const float SingleLaneW = 100.f;
const int LANE_TOTAL = 6;
const float SplitLineThick = 3.f;

// 判定窗口常量
const int JUDGE_INTACT = 100;
const int JUDGE_FRACTURED = 170;

// 音符下落速度与长按计分常量
const float fallSpeedPxPerSec = 1000.f;
const int HOLD_COMPOSE_SCORE = 80;
const int HOLD_RELEASE_BUFFER = 300;

// 轨道编号常量
const int LANE_SLIDE_LEFT = 0;
const int LANE_D = 1;
const int LANE_F = 2;
const int LANE_J = 3;
const int LANE_K = 4;
const int LANE_SLIDE_RIGHT = 5;

// 音符倍速调节常量
const float minSpeed = 0.5f;
const float maxSpeed = 3.0f;
const float speedStep = 0.1f;

// 按键输入绑定结构体 
struct KeyBind
{
    sf::Keyboard::Key key;
    int targetLane;
};
const KeyBind allKeyBinds[] = {
    {sf::Keyboard::D, LANE_D},
    {sf::Keyboard::F, LANE_F},
    {sf::Keyboard::J, LANE_J},
    {sf::Keyboard::G, LANE_J},
    {sf::Keyboard::K, LANE_K},
    {sf::Keyboard::H, LANE_K}
};
const int BindCount = sizeof(allKeyBinds) / sizeof(KeyBind);
bool keyHoldState[6] = { false,false,false,false,false,false };
bool prevKeyHoldState[6] = { false,false,false,false,false,false };

// 自定义判定线指针全局资源 
sf::Texture texCursor;
sf::Sprite sprCursor;
float cursorLockY = 0.f; 

//  倍速调节UI全局控件 
float noteSpeedRate = 1.0f;
sf::RectangleShape speedBtnAdd, speedBtnSub;
sf::Text speedTextAdd, speedTextSub, speedRateShow;

// 暂停界面新增重开按钮
sf::RectangleShape btnRestartPause;
sf::Text txtRestartPause;

//结算界面重开按钮 
sf::RectangleShape btnResultRestart;
sf::Text txtResultRestart;

// 工具函数声明区域 
// 检测指定轨道按键是否处于按住状态
bool IsLaneKeyHeld(int lane)
{
    for (int i = 0; i < BindCount; i++)
    {
        if (allKeyBinds[i].targetLane == lane && keyHoldState[i])
            return true;
    }
    return false;
}

// 加载全部歌曲文件夹数据
void loadAllSongs(const std::string& songsDir, std::vector<SongInfo>& outSongList)
{
    outSongList.clear();
    namespace fs = std::filesystem;
    if (!fs::exists(songsDir))
    {
        std::cerr << "歌曲文件夹不存在：" << songsDir << std::endl;
        return;
    }
    for (const auto& dirEntry : fs::directory_iterator(songsDir))
    {
        if (!dirEntry.is_directory()) continue;
        SongInfo song;
        song.folderPath = dirEntry.path().string() + "/";
        song.title = dirEntry.path().filename().string();
        song.coverPath = song.folderPath + "cover.png";
        if (!fs::exists(song.coverPath))
        {
            std::cerr << song.title << " 缺少曲绘，跳过\n";
            continue;
        }
        song.bgmPath = song.folderPath + "bgm.ogg";
        if (!fs::exists(song.bgmPath))
        {
            std::cerr << song.title << " 缺少BGM，跳过\n";
            continue;
        }
        std::map<std::string, Difficulty> fileToDiff = {
            {"ember.txt", Difficulty::Ember},
            {"rift.txt", Difficulty::Rift},
            {"shatter.txt", Difficulty::Shatter},
            {"void.txt", Difficulty::Void}
        };
        for (const auto& [fileName, diff] : fileToDiff)
        {
            std::string chartPath = song.folderPath + fileName;
            if (fs::exists(chartPath))
                song.chartPaths[diff] = chartPath;
        }
        if (song.chartPaths.empty())
        {
            std::cerr << song.title << " 无谱面，跳过\n";
            continue;
        }
        outSongList.push_back(song);
        std::cout << "加载歌曲：" << song.title << "，难度数：" << song.chartPaths.size() << std::endl;
    }
}

// 读取谱面txt生成音符数组
bool LoadChartFromFile(const std::string& filePath, std::vector<Note>& outNotes)
{
    std::ifstream fin(filePath);
    if (!fin.is_open())
    {
        std::cerr << "无法打开谱面：" << filePath << std::endl;
        return false;
    }
    outNotes.clear();
    int lane, type, spawnTime, holdDur;
    while (fin >> lane >> spawnTime >> type >> holdDur)
    {
        Note newNote;
        newNote.lane = lane;
        newNote.spawnTime = (float)spawnTime;
        newNote.posY = -60.f;
        newNote.deleteFlag = false;
        newNote.type = (NoteType)type;
        newNote.isJudged = false;
        newNote.isSlideHit = false; // 滑条初始未命中
        newNote.holdDuration = (float)holdDur;
        newNote.holdStarted = false;
        newNote.holdPassTime = 0.f;
        newNote.startJudge = Lost;
        newNote.releaseBufferTimer = 0.f;
        newNote.slideLeftX_raw = lane * SingleLaneW;
        newNote.slideRightX_raw = (lane + 1) * SingleLaneW;
        if (newNote.type == Mine)
        {
            if (newNote.lane == LANE_SLIDE_LEFT || newNote.lane == LANE_SLIDE_RIGHT)
                continue;
        }
        outNotes.push_back(newNote);
    }
    fin.close();
    std::cout << "谱面加载完成，音符总数：" << outNotes.size() << std::endl;
    return true;
}

// 重置游戏对局数据（抽离复用函数，扩充代码行数）
void ResetGameData(std::vector<Note>& allNotesRef, std::vector<Note>& activeNotesRef, int& spawnIdxRef, float& gameTimeRef, int& scoreRef, int& comboRef, int& maxComboRef, int& intactRef, int& fracRef, int& lostRef, float& judgeFlash, float& judgeShow)
{
    activeNotesRef.clear();
    spawnIdxRef = 0;
    gameTimeRef = 0.f;
    scoreRef = 0;
    comboRef = 0;
    maxComboRef = 0;
    intactRef = 0;
    fracRef = 0;
    lostRef = 0;
    judgeFlash = 0.f;
    judgeShow = 0.f;
}

std::vector<SongInfo> songList;
int currentSongIndex = 0;
int currentDiffIndex = 0;
std::vector<Difficulty> availableDiffs;

sf::Sprite coverSprite;
sf::Texture coverTexture;

sf::RectangleShape btnLeft, btnRight;
sf::Text btnLeftText, btnRightText;

std::vector<sf::RectangleShape> diffButtons(4);
std::vector<sf::Text> diffTexts(4);
const std::vector<std::string> diffNames = { "Ember", "Rift", "Shatter", "Void" };

sf::RectangleShape btnStart;
sf::Text btnStartText;

sf::Text songTitleText;

// 暂停界面UI
bool isPaused = false;
sf::RectangleShape pauseMask;
sf::Text pauseTitle;
sf::RectangleShape btnContinue, btnExitSong;
sf::Text txtContinue, txtExitSong;

// 全部UI资源初始化函数
void initSelectSongUI(sf::Font& font)
{
    // 歌曲切换按钮初始化
    btnLeft.setFillColor(sf::Color(100, 100, 100));
    btnLeftText.setString("<");
    btnLeftText.setFont(font);
    btnLeftText.setFillColor(sf::Color::White);

    btnRight.setFillColor(sf::Color(100, 100, 100));
    btnRightText.setString(">");
    btnRightText.setFont(font);
    btnRightText.setFillColor(sf::Color::White);

    // 难度按钮颜色数组
    const std::vector<sf::Color> diffColors = {
        sf::Color(100, 200, 100),
        sf::Color(100, 150, 200),
        sf::Color(200, 100, 100),
        sf::Color(150, 100, 200)
    };
    for (int i = 0; i < 4; i++)
    {
        diffButtons[i].setFillColor(diffColors[i]);
        diffTexts[i].setFont(font);
        diffTexts[i].setFillColor(sf::Color::White);
        diffTexts[i].setString(diffNames[i]);
    }

    // 开始游戏按钮
    btnStart.setFillColor(sf::Color(255, 150, 150));
    btnStartText.setString("Start");
    btnStartText.setFont(font);
    btnStartText.setFillColor(sf::Color::White);

    // 歌曲标题文字
    songTitleText.setFont(font);
    songTitleText.setFillColor(sf::Color::White);

    // 暂停遮罩与标题
    pauseMask.setFillColor(sf::Color(0, 0, 0, 180));
    pauseTitle.setFont(font);
    pauseTitle.setString("PAUSED");
    pauseTitle.setCharacterSize(60);
    pauseTitle.setFillColor(sf::Color::White);

    // 暂停继续按钮
    btnContinue.setFillColor(sf::Color(60, 180, 80));
    txtContinue.setFont(font);
    txtContinue.setString("Continue");
    txtContinue.setFillColor(sf::Color::White);

    // 暂停退出选歌按钮
    btnExitSong.setFillColor(sf::Color(180, 60, 60));
    txtExitSong.setFont(font);
    txtExitSong.setString("Exit To Song Select");
    txtExitSong.setFillColor(sf::Color::White);

    // 暂停界面重开谱面按钮初始化
    btnRestartPause.setFillColor(sf::Color(220, 160, 0));
    txtRestartPause.setFont(font);
    txtRestartPause.setString("Restart Chart");
    txtRestartPause.setFillColor(sf::Color::White);

    // 结算界面重开按钮初始化
    btnResultRestart.setFillColor(sf::Color(220, 160, 0));
    txtResultRestart.setFont(font);
    txtResultRestart.setString("Play Again");
    txtResultRestart.setFillColor(sf::Color::White);

    // 音符倍速调节控件初始化
    speedBtnAdd.setFillColor(sf::Color(60, 120, 200));
    speedBtnSub.setFillColor(sf::Color(200, 80, 60));
    speedTextAdd.setFont(font);
    speedTextSub.setFont(font);
    speedRateShow.setFont(font);
    speedTextAdd.setString("+");
    speedTextSub.setString("-");
    speedRateShow.setFillColor(sf::Color::White);
    char initSpeedBuf[16];
    sprintf(initSpeedBuf, "Speed:%.1fx", noteSpeedRate);
    speedRateShow.setString(initSpeedBuf);

    // 自定义判定指针贴图加载
    if (texCursor.loadFromFile("res/cursor.png"))
    {
        sprCursor.setTexture(texCursor);
        float cursorScale = 0.6f;
        sprCursor.setScale(cursorScale, cursorScale);
        sf::Vector2u texSize = texCursor.getSize();
        sprCursor.setOrigin(texSize.x / 2.f, texSize.y);
    }
    else
    {
        std::cerr << "警告：res/cursor.png 指针贴图加载失败！\n";
    }
}

// 窗口尺寸变化时更新全部UI坐标布局
void updateUILayout(sf::RenderWindow& win)
{
    float winW = win.getSize().x;
    float winH = win.getSize().y;

    // 封面图布局
    if (coverTexture.getSize().x > 0 && coverTexture.getSize().y > 0)
    {
        float maxCoverW = winW * 0.3f;
        float maxCoverH = winH * 0.3f;
        float scale = std::min(maxCoverW / coverTexture.getSize().x, maxCoverH / coverTexture.getSize().y);
        coverSprite.setScale(scale, scale);
        coverSprite.setPosition((winW - coverSprite.getGlobalBounds().width) / 2.f, winH * 0.2f);
    }

    // 歌曲标题文字位置
    songTitleText.setCharacterSize((unsigned int)(winH * 0.04f));
    songTitleText.setPosition((winW - songTitleText.getGlobalBounds().width) / 2.f, winH * 0.5f);

    // 左右切歌按钮尺寸与坐标
    float btnSizeW = winW * 0.08f;
    float btnSizeH = winH * 0.15f;
    btnLeft.setSize({ btnSizeW, btnSizeH });
    btnRight.setSize({ btnSizeW, btnSizeH });
    btnLeft.setPosition(winW * 0.1f, winH * 0.4f);
    btnRight.setPosition(winW * 0.82f, winH * 0.4f);

    // 切歌按钮文字居中
    unsigned int btnTextSize = (unsigned int)(winH * 0.05f);
    btnLeftText.setCharacterSize(btnTextSize);
    btnRightText.setCharacterSize(btnTextSize);
    btnLeftText.setPosition(
        btnLeft.getPosition().x + (btnSizeW - btnLeftText.getGlobalBounds().width) / 2,
        btnLeft.getPosition().y + (btnSizeH - btnLeftText.getGlobalBounds().height) / 2
    );
    btnRightText.setPosition(
        btnRight.getPosition().x + (btnSizeW - btnRightText.getGlobalBounds().width) / 2,
        btnRight.getPosition().y + (btnSizeH - btnRightText.getGlobalBounds().height) / 2
    );

    // 难度选择按钮布局
    float diffBtnSizeW = winW * 0.08f;
    float diffBtnSizeH = winH * 0.08f;
    float diffStartX = winW * 0.3f;
    float diffY = winH * 0.75f;
    float diffSpacing = winW * 0.1f;
    for (int i = 0; i < 4; i++)
    {
        diffButtons[i].setSize({ diffBtnSizeW, diffBtnSizeH });
        diffButtons[i].setPosition(diffStartX + i * diffSpacing, diffY);
        diffTexts[i].setCharacterSize((unsigned int)(winH * 0.03f));
        diffTexts[i].setPosition(
            diffButtons[i].getPosition().x + (diffBtnSizeW - diffTexts[i].getGlobalBounds().width) / 2,
            diffButtons[i].getPosition().y + (diffBtnSizeH - diffTexts[i].getGlobalBounds().height) / 2
        );
    }

    // 开始游戏按钮布局
    float startBtnW = winW * 0.15f;
    float startBtnH = winH * 0.08f;
    btnStart.setSize({ startBtnW, startBtnH });
    btnStart.setPosition(winW * 0.7f, diffY);
    btnStartText.setCharacterSize((unsigned int)(winH * 0.035f));
    btnStartText.setPosition(
        btnStart.getPosition().x + (startBtnW - btnStartText.getGlobalBounds().width) / 2,
        btnStart.getPosition().y + (startBtnH - btnStartText.getGlobalBounds().height) / 2
    );

    // 暂停界面遮罩全屏
    pauseMask.setSize({ winW, winH });
    pauseTitle.setPosition((winW - pauseTitle.getGlobalBounds().width) / 2, winH * 0.25f);

    float pauseBtnW = winW * 0.22f;
    float pauseBtnH = winH * 0.07f;
    float btnCenterX = (winW - pauseBtnW) / 2;

    // 暂停继续按钮
    btnContinue.setSize({ pauseBtnW, pauseBtnH });
    btnContinue.setPosition(btnCenterX, winH * 0.42f);
    txtContinue.setCharacterSize((unsigned int)(winH * 0.03f));
    txtContinue.setPosition(
        btnContinue.getPosition().x + (pauseBtnW - txtContinue.getGlobalBounds().width) / 2,
        btnContinue.getPosition().y + (pauseBtnH - txtContinue.getGlobalBounds().height) / 2
    );

    // 暂停重开按钮
    btnRestartPause.setSize({ pauseBtnW, pauseBtnH });
    btnRestartPause.setPosition(btnCenterX, winH * 0.51f);
    txtRestartPause.setCharacterSize((unsigned int)(winH * 0.03f));
    txtRestartPause.setPosition(
        btnRestartPause.getPosition().x + (pauseBtnW - txtRestartPause.getGlobalBounds().width) / 2,
        btnRestartPause.getPosition().y + (pauseBtnH - txtRestartPause.getGlobalBounds().height) / 2
    );

    // 暂停退出按钮
    btnExitSong.setSize({ pauseBtnW, pauseBtnH });
    btnExitSong.setPosition(btnCenterX, winH * 0.60f);
    txtExitSong.setCharacterSize((unsigned int)(winH * 0.03f));
    txtExitSong.setPosition(
        btnExitSong.getPosition().x + (pauseBtnW - txtExitSong.getGlobalBounds().width) / 2,
        btnExitSong.getPosition().y + (pauseBtnH - txtExitSong.getGlobalBounds().height) / 2
    );

    // 结算界面重开按钮布局
    float resBtnW = winW * 0.25f;
    float resBtnH = winH * 0.06f;
    btnResultRestart.setSize({ resBtnW, resBtnH });
    btnResultRestart.setPosition((winW - resBtnW) / 2, winH * 0.88f);
    txtResultRestart.setCharacterSize((unsigned int)(winH * 0.03f));
    txtResultRestart.setPosition(
        btnResultRestart.getPosition().x + (resBtnW - txtResultRestart.getGlobalBounds().width) / 2,
        btnResultRestart.getPosition().y + (resBtnH - txtResultRestart.getGlobalBounds().height) / 2
    );

    // 倍速调节控件布局（左上角）
    float spBtnSize = winH * 0.04f;
    speedBtnAdd.setSize({ spBtnSize, spBtnSize });
    speedBtnSub.setSize({ spBtnSize, spBtnSize });
    speedBtnSub.setPosition(10.f, 10.f);
    speedBtnAdd.setPosition(10.f + spBtnSize + 8.f, 10.f);
    speedTextAdd.setCharacterSize((unsigned int)(spBtnSize * 0.7f));
    speedTextSub.setCharacterSize((unsigned int)(spBtnSize * 0.7f));
    speedTextSub.setPosition(speedBtnSub.getPosition().x + (spBtnSize - speedTextSub.getGlobalBounds().width) / 2,
        speedBtnSub.getPosition().y + (spBtnSize - speedTextSub.getGlobalBounds().height) / 2);
    speedTextAdd.setPosition(speedBtnAdd.getPosition().x + (spBtnSize - speedTextAdd.getGlobalBounds().width) / 2,
        speedBtnAdd.getPosition().y + (spBtnSize - speedTextAdd.getGlobalBounds().height) / 2);
    speedRateShow.setCharacterSize((unsigned int)(winH * 0.03f));
    speedRateShow.setPosition(10.f, spBtnSize + 12.f);

    // 更新指针锁定Y坐标（同步窗口判定线高度）
    cursorLockY = winH * 0.8f;
}

// 切换歌曲时刷新封面、难度列表
void updateCurrentSongInfo(sf::RenderWindow& win)
{
    if (songList.empty()) return;
    const SongInfo& song = songList[currentSongIndex];
    if (coverTexture.loadFromFile(song.coverPath))
        coverSprite.setTexture(coverTexture);
    songTitleText.setString(song.title);
    availableDiffs.clear();
    for (const auto& pair : song.chartPaths)
        availableDiffs.push_back(pair.first);
    currentDiffIndex = 0;
    updateUILayout(win);
}


int main()
{
    system("chcp 65001");
    sf::RenderWindow win(sf::VideoMode(720, 600), "Vanesce", sf::Style::Titlebar | sf::Style::Close | sf::Style::Resize);
    win.setFramerateLimit(144);
    win.setKeyRepeatEnabled(false);

    // 音符贴图加载
    sf::Texture texNote;
    if (!texNote.loadFromFile("res/note.png"))
    {
        std::cerr << "err1：音符贴图加载失败\n";
        return -1;
    }
    sf::Sprite sprNormal, sprHoldHead;
    sprNormal.setTexture(texNote);
    sprHoldHead.setTexture(texNote);

    // 对局核心变量
    sf::Music bgm;
    std::vector<Note> allNotes;
    std::vector<Note> activeNotes;
    int spawnIdx = 0;
    sf::Clock totalClock;
    float gameTime = 0.f;

    int score = 0, combo = 0;
    int intactCnt = 0, fracturedCnt = 0, lostCnt = 0;
    int maxCombo = 0;
    GameState gameState;

    // 字体资源加载
    sf::Font font;
    bool fontOk = font.loadFromFile("res/arial.ttf");
    if (!fontOk) std::cerr << "err4：字体文件加载失败\n";

    // 分数、连击文字
    sf::Text scoreText, comboText;
    scoreText.setFont(font);
    comboText.setFont(font);
    scoreText.setFillColor(sf::Color::White);
    comboText.setFillColor(sf::Color::Yellow);

    // 判定提示文字
    sf::Text judgeText;
    judgeText.setFont(font);
    judgeText.setStyle(sf::Text::Bold);
    float judgeDisplayTimer = 0.f;
    float judgeLineFlashTimer = 0.f;
    sf::RectangleShape laneHighlights[6];
    for (int i = 0; i < 6; ++i)
        laneHighlights[i].setFillColor(sf::Color(255, 255, 255, 0));

    // 结算界面全部文字控件
    sf::Text resRank; // 评级大字体
    sf::Text resTitle, resScore, resMaxCombo;
    sf::Text resIntact, resFractured, resLost;
    sf::Text resTip, resFullCombo, resAllIntact;

    resRank.setFont(font);
    resTitle.setFont(font);
    resScore.setFont(font);
    resMaxCombo.setFont(font);
    resIntact.setFont(font);
    resFractured.setFont(font);
    resLost.setFont(font);
    resTip.setFont(font);
    resFullCombo.setFont(font);
    resAllIntact.setFont(font);

    resRank.setCharacterSize(80);
    resRank.setStyle(sf::Text::Bold);

    resTitle.setString("==== Result ====");
    resTitle.setCharacterSize(36);
    resTitle.setFillColor(sf::Color::Cyan);

    resScore.setCharacterSize(28);
    resScore.setFillColor(sf::Color::White);

    resMaxCombo.setCharacterSize(28);
    resMaxCombo.setFillColor(sf::Color::Yellow);

    resIntact.setCharacterSize(28);
    resIntact.setFillColor(sf::Color(100, 255, 100));

    resFractured.setCharacterSize(28);
    resFractured.setFillColor(sf::Color(255, 220, 80));

    resLost.setCharacterSize(28);
    resLost.setFillColor(sf::Color(255, 80, 80));

    resFullCombo.setCharacterSize(26);
    resFullCombo.setFillColor(sf::Color(255, 215, 0));

    resAllIntact.setCharacterSize(26);
    resAllIntact.setFillColor(sf::Color(0, 255, 255));

    resTip.setCharacterSize(22);
    resTip.setFillColor(sf::Color(180, 180, 180));
    resTip.setString("Press ESC to back");

    // 初始化资源与界面
    loadAllSongs("res/songs", songList);
    if (songList.empty())
    {
        std::cerr << "未找到歌曲文件夹res/songs\n";
        return -1;
    }
    initSelectSongUI(font);
    updateCurrentSongInfo(win);
    updateUILayout(win);
    gameState = State_SelectSong;
    win.setMouseCursorVisible(true); // 初始选歌界面显示系统鼠标

    // 主游戏循环
    while (win.isOpen())
    {
        float dtMs = totalClock.restart().asMilliseconds();
        float dtSec = dtMs / 1000.f;
        gameTime += dtMs;

        sf::Event evt;
        while (win.pollEvent(evt))
        {
            // 窗口关闭事件
            if (evt.type == sf::Event::Closed)
            {
                win.close();
                bgm.stop();
            }
            // 窗口缩放事件，刷新全部UI布局
            if (evt.type == sf::Event::Resized)
            {
                sf::FloatRect viewRect(0, 0, (float)evt.size.width, (float)evt.size.height);
                win.setView(sf::View(viewRect));
                updateUILayout(win);
            }

            // ESC按键逻辑：暂停/退回选歌
            if (evt.type == sf::Event::KeyPressed && evt.key.code == sf::Keyboard::Escape)
            {
                if (gameState == State_Result)
                {
                    ResetGameData(allNotes, activeNotes, spawnIdx, gameTime, score, combo, maxCombo, intactCnt, fracturedCnt, lostCnt, judgeLineFlashTimer, judgeDisplayTimer);
                    gameState = State_SelectSong;
                    win.setMouseCursorVisible(true);
                }
                else if (gameState == State_Playing)
                {
                    isPaused = !isPaused;
                    if (isPaused)
                    {
                        bgm.pause();
                        win.setMouseCursorVisible(true); // 暂停显示鼠标操作按钮
                    }
                    else
                    {
                        bgm.play();
                        win.setMouseCursorVisible(false); // 恢复游戏隐藏系统鼠标
                    }
                }
            }

            // 快捷键：F1倍速加、F2倍速减、R重开谱面
            if (evt.type == sf::Event::KeyPressed)
            {
                // 选歌界面倍速快捷键
                if (evt.key.code == sf::Keyboard::F1 && gameState == State_SelectSong)
                {
                    noteSpeedRate += speedStep;
                    if (noteSpeedRate > maxSpeed) noteSpeedRate = maxSpeed;
                    char buf[16];
                    sprintf(buf, "Speed:%.1fx", noteSpeedRate);
                    speedRateShow.setString(buf);
                }
                if (evt.key.code == sf::Keyboard::F2 && gameState == State_SelectSong)
                {
                    noteSpeedRate -= speedStep;
                    if (noteSpeedRate < minSpeed) noteSpeedRate = minSpeed;
                    char buf[16];
                    sprintf(buf, "Speed:%.1fx", noteSpeedRate);
                    speedRateShow.setString(buf);
                }
                // R键重开对局
                if (evt.key.code == sf::Keyboard::R)
                {
                    if (gameState == State_Playing && isPaused)
                    {
                        const SongInfo& curSong = songList[currentSongIndex];
                        Difficulty diffSel = availableDiffs[currentDiffIndex];
                        std::string chartPath = curSong.chartPaths.at(diffSel);
                        LoadChartFromFile(chartPath, allNotes);
                        ResetGameData(allNotes, activeNotes, spawnIdx, gameTime, score, combo, maxCombo, intactCnt, fracturedCnt, lostCnt, judgeLineFlashTimer, judgeDisplayTimer);
                        bgm.play();
                        isPaused = false;
                        win.setMouseCursorVisible(false);
                        sf::Vector2i initMouse = sf::Mouse::getPosition(win);
                        sprCursor.setPosition((float)initMouse.x, cursorLockY);
                    }
                    if (gameState == State_Result)
                    {
                        const SongInfo& curSong = songList[currentSongIndex];
                        Difficulty diffSel = availableDiffs[currentDiffIndex];
                        std::string chartPath = curSong.chartPaths.at(diffSel);
                        LoadChartFromFile(chartPath, allNotes);
                        ResetGameData(allNotes, activeNotes, spawnIdx, gameTime, score, combo, maxCombo, intactCnt, fracturedCnt, lostCnt, judgeLineFlashTimer, judgeDisplayTimer);
                        gameState = State_Playing;
                        win.setMouseCursorVisible(false);
                        sf::Vector2i initMouse = sf::Mouse::getPosition(win);
                        sprCursor.setPosition((float)initMouse.x, cursorLockY);
                    }
                }
            }

            // 鼠标左键点击判定（全部界面按钮交互）
            if (evt.type == sf::Event::MouseButtonPressed && evt.mouseButton.button == sf::Mouse::Left)
            {
                sf::Vector2i mousePos = sf::Mouse::getPosition(win);
                sf::FloatRect mouseRect((float)mousePos.x, (float)mousePos.y, 1.f, 1.f);

                // 暂停界面按钮交互
                if (gameState == State_Playing && isPaused)
                {
                    // 继续游戏
                    if (btnContinue.getGlobalBounds().intersects(mouseRect))
                    {
                        isPaused = false;
                        bgm.play();
                        win.setMouseCursorVisible(false);
                    }
                    // 重开谱面
                    if (btnRestartPause.getGlobalBounds().intersects(mouseRect))
                    {
                        const SongInfo& curSong = songList[currentSongIndex];
                        Difficulty diffSel = availableDiffs[currentDiffIndex];
                        std::string chartPath = curSong.chartPaths.at(diffSel);
                        LoadChartFromFile(chartPath, allNotes);
                        ResetGameData(allNotes, activeNotes, spawnIdx, gameTime, score, combo, maxCombo, intactCnt, fracturedCnt, lostCnt, judgeLineFlashTimer, judgeDisplayTimer);
                        bgm.play();
                        isPaused = false;
                        win.setMouseCursorVisible(false);
                        sf::Vector2i initMouse = sf::Mouse::getPosition(win);
                        sprCursor.setPosition((float)initMouse.x, cursorLockY);
                    }
                    // 退回选歌界面
                    if (btnExitSong.getGlobalBounds().intersects(mouseRect))
                    {
                        ResetGameData(allNotes, activeNotes, spawnIdx, gameTime, score, combo, maxCombo, intactCnt, fracturedCnt, lostCnt, judgeLineFlashTimer, judgeDisplayTimer);
                        gameState = State_SelectSong;
                        win.setMouseCursorVisible(true);
                    }
                }

                // 选歌界面全部点击交互
                if (gameState == State_SelectSong)
                {
                    // 倍速加减按钮
                    if (speedBtnSub.getGlobalBounds().intersects(mouseRect))
                    {
                        noteSpeedRate -= speedStep;
                        if (noteSpeedRate < minSpeed) noteSpeedRate = minSpeed;
                        char buf[16];
                        sprintf(buf, "Speed:%.1fx", noteSpeedRate);
                        speedRateShow.setString(buf);
                    }
                    if (speedBtnAdd.getGlobalBounds().intersects(mouseRect))
                    {
                        noteSpeedRate += speedStep;
                        if (noteSpeedRate > maxSpeed) noteSpeedRate = maxSpeed;
                        char buf[16];
                        sprintf(buf, "Speed:%.1fx", noteSpeedRate);
                        speedRateShow.setString(buf);
                    }
                    // 切上一首
                    if (btnLeft.getGlobalBounds().intersects(mouseRect))
                    {
                        currentSongIndex = (currentSongIndex - 1 + songList.size()) % songList.size();
                        updateCurrentSongInfo(win);
                    }
                    // 切下一首
                    else if (btnRight.getGlobalBounds().intersects(mouseRect))
                    {
                        currentSongIndex = (currentSongIndex + 1) % songList.size();
                        updateCurrentSongInfo(win);
                    }
                    // 难度选择点击
                    for (int i = 0; i < (int)availableDiffs.size(); i++)
                    {
                        Difficulty diff = availableDiffs[i];
                        if (diffButtons[(int)diff].getGlobalBounds().intersects(mouseRect))
                        {
                            currentDiffIndex = i;
                            break;
                        }
                    }
                    // 开始游戏按钮
                    if (btnStart.getGlobalBounds().intersects(mouseRect))
                    {
                        const SongInfo& curSong = songList[currentSongIndex];
                        Difficulty diffSel = availableDiffs[currentDiffIndex];
                        std::string chartPath = curSong.chartPaths.at(diffSel);
                        if (!LoadChartFromFile(chartPath, allNotes))
                        {
                            std::cerr << "谱面加载失败，无法开始游戏\n";
                            return -1;
                        }
                        if (bgm.openFromFile(curSong.bgmPath)) bgm.play();
                        ResetGameData(allNotes, activeNotes, spawnIdx, gameTime, score, combo, maxCombo, intactCnt, fracturedCnt, lostCnt, judgeLineFlashTimer, judgeDisplayTimer);
                        isPaused = false;
                        gameState = State_Playing;
                        win.setMouseCursorVisible(false);
                        sf::Vector2i initMouse = sf::Mouse::getPosition(win);
                        sprCursor.setPosition((float)initMouse.x, cursorLockY);
                    }
                }

                // 结算界面重开按钮
                if (gameState == State_Result)
                {
                    if (btnResultRestart.getGlobalBounds().intersects(mouseRect))
                    {
                        const SongInfo& curSong = songList[currentSongIndex];
                        Difficulty diffSel = availableDiffs[currentDiffIndex];
                        std::string chartPath = curSong.chartPaths.at(diffSel);
                        LoadChartFromFile(chartPath, allNotes);
                        ResetGameData(allNotes, activeNotes, spawnIdx, gameTime, score, combo, maxCombo, intactCnt, fracturedCnt, lostCnt, judgeLineFlashTimer, judgeDisplayTimer);
                        gameState = State_Playing;
                        win.setMouseCursorVisible(false);
                        sf::Vector2i initMouse = sf::Mouse::getPosition(win);
                        sprCursor.setPosition((float)initMouse.x, cursorLockY);
                    }
                }
            }

            
            if (gameState == State_Playing && !isPaused && evt.type == sf::Event::MouseMoved)
            {
                float rawX = (float)evt.mouseMove.x;
                float winW = win.getSize().x;
                float laneWidth = winW / 6.f;
                float scale = laneWidth / SingleLaneW;

                
                float clampX = std::clamp(rawX, 0.f, winW);
                sprCursor.setPosition(clampX, cursorLockY);
                float cursorX = sprCursor.getPosition().x;

                // 遍历全部滑条音符，指针碰撞判定
                for (size_t n = 0; n < activeNotes.size(); n++)
                {
                    Note& note = activeNotes[n];
                    // 跳过非滑条、已命中、已删除音符
                    if (note.type != SlideNote || note.deleteFlag || note.holdStarted || note.isSlideHit)
                        continue;
                    float diffY = fabs(note.posY - cursorLockY);
                    if (diffY > JUDGE_FRACTURED)
                        continue;

                    float slideLeft = note.slideLeftX_raw * scale;
                    float slideRight = note.slideRightX_raw * scale;
                    if (cursorX >= slideLeft && cursorX <= slideRight)
                    {
                        note.holdStarted = true;
                        note.isSlideHit = true; // 标记已命中，防止重复加分
                        note.startJudge = Intact;
                        combo++;
                        if (combo > maxCombo) maxCombo = combo;
                        score += 100 * combo;
                        intactCnt++;
                        judgeDisplayTimer = 0.5f;
                        judgeLineFlashTimer = 0.15f;
                        judgeText.setString("Intact");
                        judgeText.setFillColor(sf::Color(100, 255, 100));
                    }
                }
            }

            // 键盘按键打击判定（单点/长按音符）
            if (gameState == State_Playing && !isPaused && evt.type == sf::Event::KeyPressed)
            {
                float winH = win.getSize().y;
                float judgeLineY = winH * 0.8f;
                for (int bid = 0; bid < BindCount; bid++)
                {
                    if (evt.key.code != allKeyBinds[bid].key) continue;
                    int lane = allKeyBinds[bid].targetLane;
                    int hitIdx = -1;
                    int minDiff = 9999;
                    for (size_t n = 0; n < activeNotes.size(); n++)
                    {
                        Note& nt = activeNotes[n];
                        if (nt.lane != lane || nt.deleteFlag) continue;
                        if (nt.type == HoldNote && nt.holdStarted) continue;
                        int d = abs((int)nt.posY - (int)judgeLineY);
                        if (d <= JUDGE_FRACTURED && d < minDiff)
                        {
                            minDiff = d;
                            hitIdx = n;
                        }
                    }
                    if (hitIdx == -1) continue;
                    Note& hitNote = activeNotes[hitIdx];
                    combo++;
                    if (combo > maxCombo) maxCombo = combo;
                    Judge jd = minDiff <= JUDGE_INTACT ? Intact : Fractured;
                    hitNote.startJudge = jd;
                    judgeDisplayTimer = 0.5f;
                    judgeLineFlashTimer = 0.15f;
                    if (jd == Intact)
                    {
                        judgeText.setString("Intact");
                        judgeText.setFillColor(sf::Color(100, 255, 100));
                    }
                    else
                    {
                        judgeText.setString("Fractured");
                        judgeText.setFillColor(sf::Color(255, 220, 80));
                    }
                    if (hitNote.type == NormalNote)
                    {
                        hitNote.deleteFlag = true;
                        score += jd == Intact ? 100 * combo : 50 * combo;
                        jd == Intact ? intactCnt++ : fracturedCnt++;
                    }
                    else if (hitNote.type == HoldNote)
                    {
                        hitNote.holdStarted = true;
                        score += jd == Intact ? 100 * combo : 50 * combo;
                        jd == Intact ? intactCnt++ : fracturedCnt++;
                    }
                }
            }
        }

        // ========================= 界面渲染分支 =========================
        // 选歌界面渲染
        if (gameState == State_SelectSong)
        {
            win.clear(sf::Color(40, 40, 60));
            win.draw(coverSprite);
            win.draw(songTitleText);
            win.draw(btnLeft);
            win.draw(btnLeftText);
            win.draw(btnRight);
            win.draw(btnRightText);
            // 难度按钮绘制
            for (int i = 0; i < 4; i++)
            {
                bool has = false;
                for (auto d : availableDiffs)
                    if ((int)d == i) { has = true; break; }
                if (has)
                {
                    if (availableDiffs[currentDiffIndex] == (Difficulty)i)
                    {
                        diffButtons[i].setOutlineColor(sf::Color::White);
                        diffButtons[i].setOutlineThickness(3.f);
                    }
                    else diffButtons[i].setOutlineThickness(0.f);
                    win.draw(diffButtons[i]);
                    win.draw(diffTexts[i]);
                }
                else
                {
                    diffButtons[i].setFillColor(sf::Color(50, 50, 50));
                    win.draw(diffButtons[i]);
                }
            }
            win.draw(btnStart);
            win.draw(btnStartText);
            // 倍速控件绘制
            win.draw(speedBtnSub);
            win.draw(speedTextSub);
            win.draw(speedBtnAdd);
            win.draw(speedTextAdd);
            win.draw(speedRateShow);
        }
        // 打歌游玩界面渲染
        else if (gameState == State_Playing)
        {
            win.clear(sf::Color(30, 30, 40));
            if (!isPaused)
            {
                // 更新按键按住状态
                for (int i = 0; i < 6; i++) prevKeyHoldState[i] = keyHoldState[i];
                for (int i = 0; i < BindCount; i++)
                    keyHoldState[i] = sf::Keyboard::isKeyPressed(allKeyBinds[i].key);
                float winW = win.getSize().x;
                float winH = win.getSize().y;
                float judgeLineY = winH * 0.8f;
                float laneWidth = winW / 6.f;
                float scale = laneWidth / SingleLaneW;

                // 生成到达播放时间的音符
                while (spawnIdx < allNotes.size() && gameTime >= allNotes[spawnIdx].spawnTime)
                {
                    activeNotes.push_back(allNotes[spawnIdx]);
                    spawnIdx++;
                }

                // 音符移动、判定、失效逻辑（叠加倍速）
                for (auto& note : activeNotes)
                {
                    if (!(note.type == HoldNote && note.holdStarted))
                        note.posY += fallSpeedPxPerSec * dtSec * noteSpeedRate;

                    // 单点音符超出判定线丢失
                    if (note.type == NormalNote)
                    {
                        if (!note.deleteFlag && note.posY > judgeLineY + JUDGE_FRACTURED)
                        {
                            note.deleteFlag = true;
                            combo = 0;
                            lostCnt++;
                            judgeDisplayTimer = 0.5f;
                            judgeText.setString("Lost");
                            judgeText.setFillColor(sf::Color(255, 80, 80));
                        }
                    }
                    // 长按音符逻辑
                    else if (note.type == HoldNote)
                    {
                        if (!note.holdStarted)
                        {
                            if (note.posY > judgeLineY + JUDGE_FRACTURED)
                            {
                                note.deleteFlag = true;
                                combo = 0;
                                lostCnt++;
                                judgeDisplayTimer = 0.5f;
                                judgeText.setString("Lost");
                                judgeText.setFillColor(sf::Color(255, 80, 80));
                            }
                        }
                        else
                        {
                            if (IsLaneKeyHeld(note.lane))
                            {
                                note.releaseBufferTimer = 0;
                                note.holdPassTime += dtMs;
                                if (note.holdPassTime >= note.holdDuration)
                                {
                                    note.deleteFlag = true;
                                    combo++;
                                    if (combo > maxCombo) maxCombo = combo;
                                    score += HOLD_COMPOSE_SCORE * combo;
                                    note.startJudge == Intact ? intactCnt++ : fracturedCnt++;
                                }
                            }
                            else
                            {
                                note.releaseBufferTimer += dtMs;
                                if (note.releaseBufferTimer > HOLD_RELEASE_BUFFER)
                                {
                                    note.deleteFlag = true;
                                    combo = 0;
                                    lostCnt++;
                                    judgeDisplayTimer = 0.5f;
                                    judgeText.setString("Lost");
                                    judgeText.setFillColor(sf::Color(255, 80, 80));
                                }
                            }
                        }
                    }
                    // 滑条音符超出判定线丢失
                    else if (note.type == SlideNote)
                    {
                        if (!note.holdStarted)
                        {
                            if (note.posY > judgeLineY + JUDGE_FRACTURED)
                            {
                                note.deleteFlag = true;
                                combo = 0;
                                lostCnt++;
                                judgeDisplayTimer = 0.5f;
                                judgeText.setString("Lost");
                                judgeText.setFillColor(sf::Color(255, 80, 80));
                            }
                        }
                        else note.deleteFlag = true;
                    }
                    // 地雷音符判定
                    else if (note.type == Mine)
                    {
                        if (note.isJudged)
                        {
                            float diffY = fabs(note.posY - judgeLineY);
                            if (diffY > JUDGE_FRACTURED) note.deleteFlag = true;
                            continue;
                        }
                        float diffY = fabs(note.posY - judgeLineY);
                        if (diffY <= JUDGE_FRACTURED)
                        {
                            note.isJudged = true;
                            int lane = note.lane;
                            bool pressNow = (!prevKeyHoldState[lane]) && keyHoldState[lane];
                            if (pressNow)
                            {
                                lostCnt++;
                                combo = 0;
                                judgeDisplayTimer = 0.5f;
                                judgeText.setString("Lost");
                                judgeText.setFillColor(sf::Color(255, 80, 80));
                            }
                            else
                            {
                                intactCnt++;
                                combo++;
                                if (combo > maxCombo) maxCombo = combo;
                                judgeDisplayTimer = 0.5f;
                                judgeLineFlashTimer = 0.15f;
                                judgeText.setString("Intact");
                                judgeText.setFillColor(sf::Color(100, 255, 100));
                            }
                        }
                    }
                }

                // 清除已完成/失效音符
                for (auto it = activeNotes.begin(); it != activeNotes.end();)
                {
                    if (it->deleteFlag) it = activeNotes.erase(it);
                    else ++it;
                }

                // 谱面全部演奏完毕，进入结算界面
                if (spawnIdx >= allNotes.size() && activeNotes.empty())
                {
                    gameState = State_Result;
                    bgm.stop();
                    win.setMouseCursorVisible(true);
                    resScore.setString("Score: " + std::to_string(score));
                    resMaxCombo.setString("Max Combo: " + std::to_string(maxCombo));
                    resIntact.setString("Intact: " + std::to_string(intactCnt));
                    resFractured.setString("Fractured: " + std::to_string(fracturedCnt));
                    resLost.setString("Lost: " + std::to_string(lostCnt));

                    if (maxCombo == (int)allNotes.size()) resFullCombo.setString("Full Collection");
                    else resFullCombo.setString("");
                    if (intactCnt == (int)allNotes.size()) resAllIntact.setString("Execution Intact");
                    else resAllIntact.setString("");

                    // 计算评级
                    int totalNote = (int)allNotes.size();
                    int valid = intactCnt + fracturedCnt;
                    float rate = valid == 0 ? 0.f : (float)intactCnt / valid;
                    if (intactCnt == totalNote)
                    {
                        resRank.setString("EI");
                        resRank.setFillColor(sf::Color(0, 255, 255));
                    }
                    else if (rate >= 1.f)
                    {
                        resRank.setString("EX+");
                        resRank.setFillColor(sf::Color(255, 215, 0));
                    }
                    else if (rate >= 0.95f)
                    {
                        resRank.setString("EX");
                        resRank.setFillColor(sf::Color(220, 100, 255));
                    }
                    else if (rate >= 0.80f)
                    {
                        resRank.setString("S");
                        resRank.setFillColor(sf::Color(100, 180, 255));
                    }
                    else if (rate >= 0.60f)
                    {
                        resRank.setString("A");
                        resRank.setFillColor(sf::Color(100, 255, 120));
                    }
                    else if (rate >= 0.40f)
                    {
                        resRank.setString("B");
                        resRank.setFillColor(sf::Color(255, 230, 80));
                    }
                    else
                    {
                        resRank.setString("F");
                        resRank.setFillColor(sf::Color(255, 60, 60));
                    }
                }
            }

            // 绘制轨道、分割线、判定线、音符、UI
            float winW = win.getSize().x;
            float winH = win.getSize().y;
            float judgeLineY = winH * 0.8f;
            float laneWidth = winW / 6.f;
            float scale = laneWidth / SingleLaneW;

            for (int i = 0; i < 6; i++)
            {
                laneHighlights[i].setSize({ laneWidth, winH * 0.04f });
                laneHighlights[i].setPosition(i * laneWidth, judgeLineY - winH * 0.03f);
                bool pressed = false;
                for (int b = 0; b < BindCount; b++)
                    if (allKeyBinds[b].targetLane == i && keyHoldState[b]) { pressed = true; break; }
                laneHighlights[i].setFillColor(pressed ? sf::Color(255, 255, 255, 120) : sf::Color(255, 255, 255, 0));
            }
            for (int i = 1; i < 6; i++)
            {
                sf::RectangleShape split({ 2.f, winH });
                split.setFillColor(sf::Color(80, 80, 80));
                split.setPosition(laneWidth * i, 0);
                win.draw(split);
            }
            for (int i = 0; i < 6; i++) win.draw(laneHighlights[i]);

            sf::Text laneKeyText;
            laneKeyText.setFont(font);
            laneKeyText.setCharacterSize((unsigned int)(winH * 0.035f));
            laneKeyText.setFillColor(sf::Color(200, 200, 200));
            std::vector<std::pair<int, std::string>> laneKeyList = {
                {1, "D"},
                {2, "F"},
                {3, "J"},
                {4, "K"}
            };
            float textY = judgeLineY + winH * 0.04f; 
            for (auto& pair : laneKeyList)
            {
                int laneIdx = pair.first;
                std::string keyName = pair.second;
                laneKeyText.setString(keyName);
                float laneCenterX = laneIdx * laneWidth + laneWidth / 2.f;
                laneKeyText.setPosition(laneCenterX - laneKeyText.getGlobalBounds().width / 2.f, textY);
                win.draw(laneKeyText);
            }

            sf::RectangleShape judgeLine;
            if (judgeLineFlashTimer > 0.f)
            {
                judgeLine.setSize({ winW, 5.f });
                judgeLine.setFillColor(sf::Color::White);
                if (!isPaused) judgeLineFlashTimer -= dtSec;
            }
            else
            {
                judgeLine.setSize({ winW, 3.f });
                judgeLine.setFillColor(sf::Color::Cyan);
            }
            judgeLine.setPosition(0, judgeLineY);
            win.draw(judgeLine);

            // 绘制音符
            for (auto& note : activeNotes)
            {
                float left = note.lane * laneWidth;
                float centerX = left + laneWidth / 2.f;
                float texW = sprNormal.getTexture()->getSize().x;
                float scaleN = laneWidth / texW;
                sprNormal.setScale(scaleN, scaleN);
                sprHoldHead.setScale(scaleN, scaleN);

                if (note.type == SlideNote)
                {
                    float l = note.slideLeftX_raw * scale;
                    float r = note.slideRightX_raw * scale;
                    sf::RectangleShape bar({ r - l, 30.f });
                    bar.setFillColor(sf::Color(255, 120, 200, 160));
                    bar.setPosition(l, note.posY);
                    win.draw(bar);
                    continue;
                }
                sprNormal.setOrigin(sprNormal.getLocalBounds().width / 2, 0);
                sprHoldHead.setOrigin(sprHoldHead.getLocalBounds().width / 2, 0);
                if (note.type == NormalNote)
                {
                    sprNormal.setPosition(centerX, note.posY);
                    win.draw(sprNormal);
                }
                else if (note.type == HoldNote)
                {
                    sprHoldHead.setPosition(centerX, note.posY);
                    win.draw(sprHoldHead);
                    float totalH = (note.holdDuration / 1000.f) * fallSpeedPxPerSec * noteSpeedRate;
                    float remain = totalH * (1.f - note.holdPassTime / note.holdDuration);
                    if (remain > 2.f)
                    {
                        sf::RectangleShape body({ laneWidth * 0.8f, remain });
                        body.setFillColor(sf::Color(100, 180, 255, 150));
                        body.setPosition(left + laneWidth * 0.1f, note.posY - remain);
                        win.draw(body);
                    }
                }
                if (note.type == Mine)
                {
                    sf::RectangleShape mine({ laneWidth * 0.9f, 25.f });
                    mine.setFillColor(sf::Color::Black);
                    mine.setOutlineColor(sf::Color::Red);
                    mine.setOutlineThickness(2.f);
                    mine.setPosition(left + laneWidth * 0.05f, note.posY);
                    win.draw(mine);
                }
            }

            // 绘制自定义指针（仅游戏未暂停）
            if (!isPaused && texCursor.getSize().x > 0)
            {
                win.draw(sprCursor);
            }

            // 分数连击文字
            comboText.setCharacterSize((unsigned int)(winH * 0.05f));
            comboText.setPosition((winW - comboText.getGlobalBounds().width) / 2, winH * 0.03f);
            scoreText.setCharacterSize((unsigned int)(winH * 0.03f));
            scoreText.setPosition(winW - scoreText.getGlobalBounds().width - 10, 10);
            win.draw(scoreText);
            win.draw(comboText);
            scoreText.setString("Score: " + std::to_string(score));
            comboText.setString("Combo: " + std::to_string(combo));

            if (!isPaused && judgeDisplayTimer > 0.f) judgeDisplayTimer -= dtSec;
            if (judgeDisplayTimer > 0.f)
            {
                judgeText.setCharacterSize((unsigned int)(winH * 0.04f));
                judgeText.setPosition((winW - judgeText.getGlobalBounds().width) / 2, winH * 0.08f);
                win.draw(judgeText);
            }

            // 暂停遮罩与按钮
            if (isPaused)
            {
                win.draw(pauseMask);
                win.draw(pauseTitle);
                win.draw(btnContinue);
                win.draw(txtContinue);
                win.draw(btnRestartPause);
                win.draw(txtRestartPause);
                win.draw(btnExitSong);
                win.draw(txtExitSong);
            }
        }
        // 结算界面渲染
        else if (gameState == State_Result)
        {
            win.clear(sf::Color(20, 20, 40));
            float winW = win.getSize().x;
            float winH = win.getSize().y;

            // 评级大字
            resRank.setCharacterSize((unsigned int)(winH * 0.13f));
            resRank.setPosition((winW - resRank.getGlobalBounds().width) / 2, winH * 0.03f);
            win.draw(resRank);

            resTitle.setCharacterSize((unsigned int)(winH * 0.06f));
            resTitle.setPosition((winW - resTitle.getGlobalBounds().width) / 2, winH * 0.16f);
            win.draw(resTitle);

            resScore.setCharacterSize((unsigned int)(winH * 0.04f));
            resScore.setPosition((winW - resScore.getGlobalBounds().width) / 2, winH * 0.26f);
            win.draw(resScore);

            resMaxCombo.setCharacterSize((unsigned int)(winH * 0.04f));
            resMaxCombo.setPosition((winW - resMaxCombo.getGlobalBounds().width) / 2, winH * 0.34f);
            win.draw(resMaxCombo);

            resIntact.setCharacterSize((unsigned int)(winH * 0.03f));
            resIntact.setPosition((winW - resIntact.getGlobalBounds().width) / 2, winH * 0.42f);
            win.draw(resIntact);

            resFractured.setCharacterSize((unsigned int)(winH * 0.03f));
            resFractured.setPosition((winW - resFractured.getGlobalBounds().width) / 2, winH * 0.49f);
            win.draw(resFractured);

            resLost.setCharacterSize((unsigned int)(winH * 0.03f));
            resLost.setPosition((winW - resLost.getGlobalBounds().width) / 2, winH * 0.56f);
            win.draw(resLost);

            // 全连提示
            if (!resFullCombo.getString().isEmpty())
            {
                resFullCombo.setCharacterSize((unsigned int)(winH * 0.03f));
                resFullCombo.setPosition((winW - resFullCombo.getGlobalBounds().width) / 2, winH * 0.64f);
                win.draw(resFullCombo);
            }
            // 完美提示
            if (!resAllIntact.getString().isEmpty())
            {
                resAllIntact.setCharacterSize((unsigned int)(winH * 0.03f));
                resAllIntact.setPosition((winW - resAllIntact.getGlobalBounds().width) / 2, winH * 0.71f);
                win.draw(resAllIntact);
            }

            resTip.setCharacterSize((unsigned int)(winH * 0.03f));
            resTip.setPosition((winW - resTip.getGlobalBounds().width) / 2, winH * 0.82f);
            win.draw(resTip);

            // 结算重开按钮
            win.draw(btnResultRestart);
            win.draw(txtResultRestart);
        }
        win.display();
    }
    bgm.stop();
    return 0;
}