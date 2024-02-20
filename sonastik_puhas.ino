#include <Keypad.h>
#define PGMT(pgm_ptr) ( reinterpret_cast< const __FlashStringHelper * >( pgm_ptr )

/*
=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
    Arduino demo/driver for HT1622-based 16 segment LCDs

    Copyright (c) 2015-2021 Martin F. Falatic
    
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
    
        http://www.apache.org/licenses/LICENSE-2.0
    
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
Arduino is little-endian
H162x commands and addresses are MSB-first (3 bit mode + 9 bit command code)
Note that the very last bit is X (don't care)
Data is LSB-first, in address-sequential 4-bit nibbles as desired.
=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
*/

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
// Helper functions and variables
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
// HT162x commands (Start with 0b100, ends with one "don't care" byte)
#define  CMD_SYS_DIS  0x00  // SYS DIS    (0000-0000-X) Turn off system oscillator, LCD bias gen [Default]
#define  CMD_SYS_EN   0x01  // SYS EN     (0000-0001-X) Turn on  system oscillator
#define  CMD_LCD_OFF  0x02  // LCD OFF    (0000-0010-X) Turn off LCD display [Default]
#define  CMD_LCD_ON   0x03  // LCD ON     (0000-0011-X) Turn on  LCD display
#define  CMD_RC_INT   0x18  // RC INT     (0001-10XX-X) System clock source, on-chip RC oscillator
#define  CMD_BIAS_COM 0x29  // BIAS & COM (0010-10X1-X) 1/3 bias, 4 commons // HT1621 only

#define  HT1622_ADDRS 0x40  // HT1622 has 64 possible 4-bit addresses

#define CS   14 // Active low
#define WR   15 // Active low
#define DATA  16

#define LSB_FORMAT  true
#define MSB_FORMAT  false

#define SETUP_DELAY_USECS 1
#define HOLD_DELAY_USECS  1
#define WRITE_DELAY_USECS 2  // Tclk min. on data sheet - overhead is more than this at low clock speeds
#define RESET_DELAY_USECS 1000  // Not strictly necessary

#define NUM_DIGITS  10

const uint8_t digitAddr[NUM_DIGITS] = {
  0x24, 0x20, 0x1C, 0x18, 0x14, 0x10, 0x0C, 0x08, 0x04, 0x00
};

const uint16_t PROGMEM SegCharDataLSBfirst[] = {
  0x0000, 0x0000, 0x122D, 0x0000, 0x0000, 0x0000, 0x0000, 0xCC0D, //  !"#$%&'
  0x0082, 0x2100, 0xA3A3, 0x8221, 0x0100, 0x8001, 0x0800, 0x0180, // ()*+,-./
  0x5C5C, 0x1A28, 0x9C59, 0x985D, 0xC045, 0xD81D, 0xDC1D, 0x1054, // 01234567
  0xDC5D, 0xD85D, 0x8800, 0x0000, 0x8809, 0x0000, 0x0000, 0x0000, // 89:;<=>?
  0x0000, 0x11D5, 0x1A3D, 0x4E28, 0x097C, 0x185D, 0xD271, 0x5410, // @ABCDEFG
  0x9D19, 0x45C4, 0x15C4, 0xC67C, 0x0174, 0x0000, 0x0000, 0x9C1D, // HIJKLMNO
  0x5454, 0xD155, 0x0000, 0x549D, 0xC045, 0x1C1C, 0x4E6E, 0x4E6C, // PQRSTUVW
  0x0000, 0xCE44, 0x23A2, 0x0000, 0x2002, 0x0000, 0x060D, 0x0808, // XYZ[\]^_
  0x0000, 0xD455, 0x1A7D, 0x5C18, 0x1A7C, 0xDC19, 0xD411, 0x5C1D, // `abcdefg
  0xC445, 0x1A38, 0x0C4C, 0xC482, 0x4C08, 0x64C4, 0x6446, 0x5C5C, // hijklmno
  0xD451, 0x5C5E, 0xD453, 0xD81D, 0x1230, 0x4C4C, 0x4580, 0x4546, // pqrstuvw
  0x2182, 0xC84D, 0x1998, 0x0000, 0x0000, 0x0000, 0x9CAD, 0x0000, // xyz{|}~
};

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
char * valToHex(int val, int prec, const char * prefix) {
      static char fmt[32], output[32];
      sprintf(fmt, "%s%%0%dX", prefix, prec);
      sprintf(output, fmt, val);
      return output;
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
// Send up to 16 bits, MSB (default) or LSB
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void HT162x_SendBits(uint16_t data, uint8_t bits, boolean LSB_FIRST = MSB_FORMAT)
{
  // Data is shifted out bitwise, either LSB-first or MSB-first.
  // The mask is used to isolate the bit being transmitted.

  uint16_t mask = LSB_FIRST ? 1 : 1 << bits-1;

  for (uint8_t i = bits; i > 0; i--)
  {
    delayMicroseconds(WRITE_DELAY_USECS);
    digitalWrite(WR, LOW);
    data & mask ? digitalWrite(DATA, HIGH) : digitalWrite(DATA, LOW);
    delayMicroseconds(WRITE_DELAY_USECS);
    digitalWrite(WR, HIGH);
    delayMicroseconds(HOLD_DELAY_USECS);
    LSB_FIRST ? data >>= 1 : data <<= 1;
  }
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void HT162x_Command(uint8_t cmd)
{
  delayMicroseconds(SETUP_DELAY_USECS);
  digitalWrite(CS, LOW);
  delayMicroseconds(SETUP_DELAY_USECS);
  HT162x_SendBits(0b100, 3);
  HT162x_SendBits(cmd, 8);
  HT162x_SendBits(1, 1);
  delayMicroseconds(SETUP_DELAY_USECS);
  digitalWrite(CS, HIGH);
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void HT162x_WriteData(uint8_t addr, uint16_t sdata, uint8_t bits = 4)
{
  // Note: bits needs to be a multiple of 4 as data is in nibbles
  delayMicroseconds(SETUP_DELAY_USECS);
  digitalWrite(CS, LOW);
  delayMicroseconds(SETUP_DELAY_USECS);
  HT162x_SendBits(0b101, 3);
  HT162x_SendBits(addr, 6);
  for (int n = (bits/4)-1; n >= 0; n--) {
    uint16_t nib = (sdata & (0xf) << 4*n) >> (4*n);
    HT162x_SendBits(nib, 4, LSB_FORMAT);
  }
  delayMicroseconds(SETUP_DELAY_USECS);
  digitalWrite(CS, HIGH);
}

void AllElements(uint8_t state)
{
  for (uint8_t addr = 0; addr < HT1622_ADDRS; addr++)
  {
    HT162x_WriteData(addr, (state ? 0xF : 0x0), 4);
  }
}

void AllSegments(uint8_t state)
{
  for (uint8_t pos = 0; pos < NUM_DIGITS; pos++)
  {
    HT162x_WriteData(digitAddr[pos], (state ? 0xFFFF : 0x0000), 16);
  }
}

void Display(char word[], int word_len, int position)
{
  for (int n = 0; n + position < word_len && n < 10 ; n++)
  {
    HT162x_WriteData(digitAddr[n], pgm_read_word_near(SegCharDataLSBfirst + (int)word[n + position] - 32), 16);
  }

}

// END OF LCD DRIVER FUNCTIONS

const byte rows = 8;  // eight rows
const byte cols = 6;  // six columns
char keys[rows][cols] = {
  {  1,  2,  3,  4,  5,  6, },
  { 10, 11, 12, 13, 14, 15, },
  { 19, 20, 21, 22, 23, 24, },
  { 28, 29, 30, 31, 32, 33, },

  {  7,  8,  9,  37,  41,  0, },
  { 16, 17, 18,  38,  42,  0, },
  { 25, 26, 27,  39,  43,  0, },
  { 34, 35, 36,  40,  44,  0, },
};
byte rowPins[rows] = { 13, 12, 11, 10, 9, 8, 7, 6, };        // connect to the row pinouts of the keypad
byte colPins[cols] = { 5, 4, 3, 2, 1, 0, };  // connect to the column pinouts of the keypad
Keypad keyboard = Keypad(makeKeymap(keys), rowPins, colPins, rows, cols);

const char latin[34] =    "abcdefghijklmnopqrstuvwxyz~AOUS   ";
const char cyrillic[34] = "aBbGDeHZ3IJkLmhoPpctyFxCTWV\"Y\'EKQ ";

const char PROGMEM estonian[] = "\naasta\nabi\naeg\naga\nainult\nainus\naitama\najal\najama\naken\naktsia\nalati\nalgama\nalgus\nall\nalla\nalles\nalus\nalustama\namet\nandma\nandmed\naru\narvama\nasi\nastuma\nasuma\naugust\nauto\navama\nedasi\neelmine\nees\neesmArk\neest\neesti\neestlane\nega\nehitama\nehk\nei\neile\neks\nelama\nelu\nema\nenam\nendine\nenne\nent\nerakond\nerinev\neriti\nesimees\nesimene\nesitama\net\nette\nettev~te\nfilm\nfirma\nhakkama\nhea\nheitma\nhetk\nhiljem\nhind\nhing\nhinnang\nhoidma\nhommik\nhoopis\nhulk\nhuvi\nhAsti\nhAAl\niga\nikka\nikkagi\nilm\nilma\nilmselt\nilmuma\nilus\ninimene\nisa\nise\nisegi\nistuma\nja\njah\njalg\njaoks\njooksma\njooksul\nju\njuba\njuhataja\njuht\njuhtuma\njumal\njust\njutt\njuurde\njuures\nj~ud\nj~udma\njAlle\njArel\njArele\njArgi\njArgmine\njAtkama\njAtma\njAAma\nka\nkaasa\nkaduma\nkaks\nkandma\nkaotama\nkartma\nkas\nkasutama\nkasvama\nkeegi\nkeel\nkeha\nkell\nkes\nkiiresti\nkindel\nkindlasti\nkinni\nkinnitama\nkiri\nkirjutama\nkodu\nkoer\nkogu\nkohalik\nkohe\nkoht\nkohta\nkohus\nkokku\nkolm\nkolmas\nkool\nkoos\nkord\nkorraldama\nkorter\nkostma\nkroon\nkuhu\nkui\nkuid\nkuidagi\nkuidas\nkuigi\nkuna\nkunagi\nkuni\nkus\nkust\nkutsuma\nkuu\nkuulama\nkuulma\nkuuluma\nk~ige\nk~ik\nk~rge\nk~rval\nk~rvale\nkAima\nkAsi\nkUll\nkUlm\nkUmme\nkUsima\nkUsimus\nlaev\nlahkuma\nlahti\nlangema\nlaps\nlaskma\nlaud\nlausuma\nleidma\nleping\nligi\nlihtsalt\nliiga\nliige\nliikuma\nliit\nlinn\nlisa\nlisama\nlooma\nlootma\nlubama\nlugema\nlugu\nl~petama\nl~pp\nl~ppema\nl~puks\nlAbi\nlOOma\nmaa\nmaailm\nmagama\nmaha\nmaja\nmaksma\nmeel\nmeeldima\nmeenutama\nmees\nmeri\nmets\nmiks\nmiljon\nmilline\nmina\nminema\nmingi\nmis\nmiski\nmitte\nmitu\nmuidu\nmuidugi\nmust\nmuu\nmuutma\nmuutuma\nm~istma\nm~lema\nm~ni\nm~te\nm~tlema\nmAletama\nmAng\nmAngima\nmArkama\nmArkima\nmArts\nmOOda\nmUUma\nnaerma\nnagu\nnaine\nneli\nnii\nniisugune\nnimetama\nnimi\nning\nnoor\nnoormees\nn~nda\nn~udma\nn~ukogu\nnAdal\nnAgema\nnAgu\nnAima\nnAitama\nnAiteks\nnUUd\nolema\nolev\nolukord\noluline\noma\nomanik\nometi\nootama\nosa\noskama\nostma\notsa\notse\notsima\notsus\notsustama\npaar\npaistma\npakkuma\npalju\npalk\npaluma\npanema\npank\nparem\nparim\npea\npeaaegu\npeale\npeaminister\npidama\npiir\npikk\npilk\npilt\npisut\npoeg\npoiss\npoliitiline\npolitsei\npool\npoole\npraegu\npraegune\npresident\nprobleem\nprotsent\npuhas\npuhul\npunane\npuu\np~hjus\npAev\npArast\npAris\npAAsema\npOOrama\npOOrduma\npUUdma\nraamat\nraha\nrahvas\nraske\nriigikogu\nriik\nringi\nrohkem\nruum\nrAAkima\nsaama\nsaatma\nsama\nsamas\nsamm\nsamuti\nsattuma\nseadus\nseal\nsealt\nsee\nsein\nseisma\nselg\nselge\nselgitama\nselline\nseni\nsest\nsiduma\nsiia\nsiin\nsiis\nsiiski\nsilm\nsina\nsinna\nsisse\nsoovima\nsuhe\nsurm\nsuu\nsuur\nsuurem\nsuutma\nsuvi\ns~ber\ns~itma\ns~na\nsOOma\nsUda\nsUndima\ntaas\ntaga\ntagasi\ntaha\ntahtma\nteadma\nteatama\ntee\ntegelikult\ntegema\ntegemine\ntegevus\nteine\ntekkima\ntema\nterve\ntoetama\ntohtima\ntoimuma\ntoo\ntooma\ntuba\ntuhat\ntulema\ntulemus\ntuli\ntund\ntundma\ntunduma\ntunne\ntunnistama\nturg\ntuttav\ntuul\nt~esti\nt~mbama\nt~stma\nt~ttu\nt~usma\ntAhendama\ntAiesti\ntAis\ntAitma\ntAna\ntAnav\ntApselt\ntOO\ntOOtaja\ntOOtama\ntUdruk\ntUhi\ntUtar\nuks\numbes\nuskuma\nuuesti\nuurima\nuus\nvaatama\nvaba\nvahel\nvahele\nvaid\nvaja\nvajalik\nvajama\nvalge\nvalima\nvalimine\nvalitsus\nvalmis\nvana\nvanem\nvarem\nvastama\nvastu\nvastus\nveel\nveidi\nvene\nvesi\nviima\nviimane\nviis\nvist\nv~i\nv~ib-olla\nv~ima\nv~imalik\nv~imalus\nv~itma\nv~tma\nv~~ras\nvAga\nvAhem\nvAhemalt\nvAike\nvAitma\nvAlja\n~hk\n~htu\n~ige\n~igus\n~nn\n~petaja\n~ppima\nAkki\nAra\nOO\nUks\nUldse\nUle\nUles\nUlikool\nUmber\nUsna\nUtlema\n";
const int estonian_numbers[] = { };
const char PROGMEM english[] = "\na little 1\na little 2\na lot\nabout 1\nabout 2\nabsolutely\naccording to\naction\nactually\nadd 1\nadd 2\nadmit\nafter 1\nafter 2\nafter 3\nafter all\nagain 1\nagain 2\nagain 3\nagainst\nage\nahead\nair\nallow\nalmost\nalready\nalthough\nalways\namount\nand 1\nand 2\nanother\nanswer 1\nanswer 2\nanymore\nanyway\napartment\napparently\nappear 1\nappear 2\nappear 3\narise\naround 1\naround 2\narrange\nas well\naside\nask 1\nask 2\nassessment\nat 1\nat 2\nat all\nat least\naugust\naway\nback 1\nback 2\nbank\nbasis\nbe\nbe able to 1\nbe able to 2\nbe afraid of\nbe allowed\nbe born\nbe heard\nbeautiful\nbecause 1\nbecause 2\nbecause of\nbefore\nbegin\nbeginning\nbehind 1\nbehind 2\nbelieve\nbelong to\nbelow\nbeside\nbest\nbetter\nbetween\nbigger\nbind\nblack\nbody\nbook\nborder\nboth\nboy\nbring\nbuild\nbut 1\nbut 2\nbut 3\nbuy\ncall\ncame\ncar\ncase\ncast\ncatch\ncertain\nchairman\nchange 1\nchange 2\nchild\nchoose\nchoosing\ncity\nclaim\nclean\nclear\nclose to\nclosed\ncold\ncollection\ncome\ncompany 1\ncompany 2\nconfirm\ncontinue\ncontract\ncorrect\ncouncil\ncountry\ncouple\ncourt\ncreate\ncrown\ncurrent one\ndata\ndaughter\nday\ndeath\ndecide\ndecision\ndefinitely\ndemand\ndifferent\ndifficult\ndirectly\ndo\ndog\ndoor\ndown 1\ndown 2\ndrive 1\ndrive 2\nduring 1\nduring 2\nearlier\neat\neither\nemployee\nempty\nend 1\nend 2\nend 3\nescape\nespecially\nestonian 1\nestonian 2\neven\nevening\never\neverything 1\neverything 2\nexactly 1\nexactly 2\nexplain\neye\nface\nfall\nfamiliar\nfast\nfather\nfeel\nfeeling\nfinally\nfind\nfirst\nfive\nfor 1\nfor 2\nfor example\nforest\nformer\nforward\nfour\nfree\nfriend\nfrom there\nfrom where\nfulfill\nfull\ngame\nget\nget into\nget up\ngirl\ngive\ngo\ngo away\ngod\ngood\ngovernment\ngrow\nhand\nhandle it\nhappen\nhead\nhealthy\nhear\nheart\nhelp 1\nhelp 2\nhere 1\nhere 2\nhigh\nhim\nhimself\nhit\nhome\nhope\nhour\nhouse\nhow\nhowever 1\nhowever 2\nhowever 3\nhuman\ni guess\nif\nimmediately\nimportant\nin\nin front\ninstead\ninterest\ninvestigate\nkeep 1\nkeep 2\nknow\nland\nlanguage\nlarge\nlast one\nlater\nlaugh\nlaw\nlearn\nleave 1\nleave 2\nleg\nless\nlet\nletter\nlife\nlift\nlike 1\nlike 2\nlike that\nlike this\nlisten\nlive\nlocal\nlong\nlook\nlook for\nlose\nluck\nmaking\nman\nmanager 1\nmanager 2\nmarch\nmarket\nme\nmean\nmember\nmillion\nmind\nmoment\nmoney\nmonth\nmore\nmorning\nmother\nmouth\nmove\nmovie\nmultiple\nname 1\nname 2\nnation\nnecessary\nneed 1\nneed 2\nnew\nnext\nnight\nno\nnor\nnot\nnote\nnotice\nnow\nof course\noffer\nold\non\none\nonly 1\nonly 2\nopen 1\nopen 2\nopportunity\nor\notherwise\nout\nover\nown\nowner\nparent\nparliament\npart\nparty\npast\npay\npercentage\nperhaps 1\nperhaps 2\npicture\nplace\nplay\npolice\npolitical\npossible\npower 1\npower 2\npresent 1\npresent 2\npresident\nprevious\nprice\nprime minister\nproblem\nprofession\npull\npurpose\nput\nquestion\nquite\nreach\nread\nready\nreal\nreally\nreason\nred\nrelationship\nremember 1\nremember 2\nreport\nresult\nright 1\nright 2\nright now\nroad\nroom 1\nroom 2\nrun\nrussian\nsalary\nsame\nsay\nschool\nsea\nsee 1\nsee 2\nseem\nsell 1\nsell 2\nsend\nsettle down\nship\nshow\nside\nsimply\nsit down\nsituation\nsleep\nsmall\nso\nso far\nsome\nsome kind\nsomehow\nsomeone\nsomething\nsometimes\nson\nsoul\nstand\nstart\nstay\nstep 1\nstep 2\nstill 1\nstill 2\nstock\nstop\nstory\nstranger\nstreet\nsuch\nsuddenly\nsummer\nsupport\ntable\ntake 1\ntake 2\ntake place\ntalk 1\ntalk 2\nteacher\nten\nthat 1\nthat 2\nthen\nthere 1\nthere 2\nthing\nthink 1\nthink 2\nthird\nthis\nthough\nthought\nthousand\nthree\nthrough\ntime 1\ntime 2\nto\ntoday\ntogether 1\ntogether 2\ntoo 1\ntoo 2\ntowards\ntree\nturn 1\nturn 2\ntwo\nunderstand\nunderstood\nunion\nuniversity\nuntil\nup\nuse\nutter\nwait\nwall\nvanish\nwant to\nwater\nwear\nweather\nweek\nwell\nvery\nwhat\nwhere 1\nwhere 2\nwhich one\nwhite\nwho\nwhy\nwin\nwind\nwindow\nwish\nwith\nwithout\nvoice\nwoman\nword\nwork 1\nwork 2\nworld\nwrite\nyear\nyes\nyesterday\nyet 1\nyet 2\nyou\nyoung\nyoung man\n";
const int PROGMEM english_numbers[] = { 310, 464, 294, 143, 439, 426, 110, 397, 394, 200, 201, 416, 108, 109, 328, 95, 107, 386, 441, 461, 76, 57, 485, 204, 302, 96, 160, 11, 72, 89, 262, 398, 460, 462, 46, 78, 152, 81, 82, 271, 292, 399, 340, 500, 151, 349, 175, 181, 296, 68, 104, 128, 496, 481, 27, 493, 357, 388, 298, 17, 275, 284, 378, 121, 403, 385, 153, 83, 161, 362, 423, 48, 12, 13, 387, 389, 440, 170, 14, 174, 300, 299, 447, 377, 363, 237, 127, 334, 306, 242, 312, 406, 38, 3, 157, 448, 285, 166, 411, 28, 323, 63, 333, 131, 53, 239, 240, 187, 453, 454, 199, 483, 322, 358, 193, 133, 179, 139, 409, 58, 60, 134, 112, 192, 487, 267, 339, 291, 144, 202, 154, 318, 21, 437, 327, 374, 290, 289, 132, 266, 51, 337, 287, 395, 138, 438, 15, 216, 8, 381, 7, 94, 459, 383, 122, 433, 436, 208, 209, 286, 330, 52, 35, 36, 87, 486, 162, 171, 172, 101, 431, 359, 368, 270, 186, 418, 130, 85, 413, 415, 210, 191, 54, 469, 34, 92, 273, 224, 47, 30, 257, 445, 380, 353, 165, 428, 427, 247, 344, 350, 424, 435, 20, 176, 229, 100, 62, 455, 124, 177, 61, 99, 301, 401, 169, 384, 1, 6, 364, 365, 173, 400, 86, 212, 137, 203, 412, 217, 159, 49, 347, 367, 84, 470, 156, 141, 278, 371, 32, 71, 73, 442, 69, 305, 391, 213, 126, 376, 468, 65, 254, 351, 491, 113, 184, 91, 480, 188, 135, 44, 422, 220, 255, 259, 265, 168, 43, 140, 307, 308, 288, 120, 489, 396, 222, 97, 98, 251, 417, 228, 425, 196, 226, 219, 64, 335, 167, 341, 70, 45, 375, 197, 59, 234, 260, 261, 336, 450, 449, 451, 443, 111, 494, 40, 37, 233, 250, 249, 274, 236, 293, 457, 303, 495, 4, 5, 29, 185, 475, 471, 235, 484, 497, 279, 280, 458, 338, 283, 50, 252, 218, 321, 39, 472, 309, 142, 248, 314, 313, 474, 105, 473, 55, 276, 319, 31, 66, 304, 320, 19, 421, 33, 297, 182, 501, 106, 205, 456, 329, 420, 326, 324, 373, 221, 246, 392, 410, 42, 488, 317, 393, 342, 407, 93, 465, 295, 346, 502, 148, 223, 269, 444, 414, 238, 253, 345, 26, 183, 272, 315, 194, 88, 277, 215, 482, 258, 361, 243, 230, 158, 125, 232, 446, 311, 67, 356, 18, 114, 25, 348, 16, 77, 10, 207, 206, 478, 430, 360, 492, 379, 402, 189, 467, 477, 404, 102, 343, 490, 180, 56, 405, 366, 352, 370, 24, 23, 245, 147, 354, 178, 244, 408, 146, 211, 2, 150, 103, 429, 145, 149, 115, 195, 316, 325, 331, 332, 118, 241, 22, 198, 499, 163, 498, 123, 190, 282, 355, 117, 390, 466, 119, 79, 268, 74, 479, 231, 155, 164, 227, 452, 129, 225, 476, 419, 9, 372, 116, 80, 75, 256, 382, 432, 434, 214, 136, 0, 90, 41, 281, 463, 369, 263, 264, };
const char PROGMEM german[] = "\nabend\naber 1\naber 2\naber 3\nabsolut\naktie\naktion\naktuelle\nalles 1\nalles 2\nalso 1\nalso 2\nalt\nalter\nan\nanbieten\nanderer\nanders\nanfang\nanfangen\nangst haben vor\nanruf\nansonsten\nantwort\nantworten\narbeiten 1\narbeiten 2\narrangieren\nart\naufstehen\nauge\naugust\naus\nauto\nbank\nbasis\nbauen\nbaum\nbeanspruchen\nbeenden\nbeginnen\nbehalten 1\nbehalten 2\nbei 1\nbei 2\nbeide\nbein\nbeiseite\nbekommen\nbemerken\nbenennen\nbenutzen\nbereit\nbereits\nberuf\nberuhigen\nbesitzer\nbesonders\nbesser\nbeste\nbestimmt\nbestAtigen\nbezahlen\nbeziehung\nbewegen\nbewertung\nbild\nbinden\nbis\nbis jetzt\nbleiben\nbrauchen 1\nbrauchen 2\nbrief\nbringen\nbuch\ndann\ndas 1\ndas 2\ndass\ndaten\ndefinitiv\ndenken 1\ndenken 2\nding\ndirekt\ndort 1\ndort 2\ndrehen 1\ndrehen 2\ndrei\ndritte\ndu\ndurch\neigen\neinfach\neins\neintausend\nelternteil\nende 1\nende 2\nendlich\nentscheiden\nentscheidung\nentsprechend\nentstehen\nentweder\nerfUllen\nergebnis\nerinnern 1\nerinnern 2\nerklAren\nerlauben\nerlaubt sein\nerreichen\nerschaffen\nerscheinen 1\nerscheinen 2\nerscheinen 3\nerscheinen 4\nerste\nessen\nestnisch 1\nestnisch 2\netwas\netwas melden\nfahren 1\nfahren 2\nfall\nfallen\nfangen\nfast\nfenster\nfilm\nfinden\nflucht\nfordern\nfrage\nfragen 1\nfragen 2\nfrau\nfrei\nfremder\nfreund\nfrUher\nfUhlen\nfUnf\nfUr 1\nfUr 2\nganz\ngeben\ngeboren werden\ngedanke\ngefUhl\ngegen\ngegenwArtig 1\ngegenwArtig 2\ngeh weg\ngehalt\ngehen\ngehOren\ngehOrt werden\ngeist\ngeld\ngelegenheit\ngenau 1\ngenau 2\ngenau genommen\ngenug\ngeraten in\ngericht\ngeschichte\ngeschlossen\ngesetz\ngesicht\ngestern\ngesund\ngewinnen\ngieSen\nglauben\ngleiche\ngleichzeitig\nglUck\ngott\ngrenze\ngroS\ngrund\ngrOSer\ngut\nhand\nhaus\nheben\nheim\nhelfen\nherstellung\nherz\nheute\nhier 1\nhier 2\nhilfe\nhinzufUgen 1\nhinzufUgen 2\nhinter 1\nhinter 2\nhoch 1\nhoch 2\nhoffen\nhund\nhOren 1\nhOren 2\nich schAtze\nihn\nim augenblick\nimmer\nin\nin der lage sein 1\nin der lage sein 2\nin richtung\ninteresse\nirgendwie\nja\njahr\njedoch 1\njedoch 2\njemand\njetzt\njung\njunge\njunger mann\nkalt\nkam\nkaufen\nkind\nklar\nklein\nkommen\nkopf\nkrone\nkOrper\nkUmmere dich darum\nlachen\nland 1\nland 2\nlang\nlassen\nlaufen\nleben 1\nleben 2\nleer\nlehrer\nleistung 1\nleistung 2\nlernen\nlesen\nletzte\nlokal\nluft\nmachen\nmanager 1\nmanager 2\nmanche\nmanchmal\nmann\nmarkt\nmarsch\nmauer\nmeer\nmehr 1\nmehr 2\nmehrere\nmeinen\nmenge 1\nmenge 2\nmenschlich\nmich\nmillion\nmindestens\nmit\nmitarbeiter\nmitglied\nmoment\nmonat\nmorgen\nmund\nmutter\nmAdchen\nmOchte\nmOgen\nmOglich\nnach 1\nnach 2\nnach 3\nnach vorne\nnacht\nnahe bei\nname\nnation\nnatUrlich\nneben\nnehmen 1\nnehmen 2\nnein\nneu\nnicht\nnoch 1\nnoch 2\nnoch 3\nnotieren\nnotwendig\nnur 1\nnur 2\nnAchste\nobwohl\noder\noffen\nohne\nort\npaar\nparlament\nparty\npassieren\nplOtzlich\npolizei\npolitisch\npreis\npremierminister\nproblem\nprozentsatz\nprAsident\nrat\nreal\nrechts 1\nrechts 2\nregierung\nrichtig\nrot\nrunter 1\nrunter 2\nrussisch\nrUcken\nsagen\nsammlung\nsauber\nscheinbar\nschicken\nschiff\nschlafen\nschlagen\nschlieSlich\nschnell\nschreiben\nschreiten\nschritt\nschule\nschwarz\nschwierig\nschOn\nseele\nsehen\nsehr\nsein\nseite\nsich hinsetzen\nsich selbst\nsituation\nso was\nso wie das\nsofort\nsogar\nsohn\nsolch\nsommer\nsowie\nspiel\nspielen\nsprache\nsprechen 1\nsprechen 2\nspAter\nstadt\nstattdessen\nstattfinden\nstehen\nstellen\nstets\nstimme\nstoppen\nstraSe\nstunde\nsuchen\nzehn\nzeigen\nzeit 1\nzeit 2\nziehen\nzimmer 1\nzimmer 2\nzu 1\nzu 2\nzu 3\nzugeben\nzum beispiel\nzurUck\nzusammen 1\nzusammen 2\nzweck\nzwei\nzwischen\ntag\nteil\ntisch\ntochter\ntod\ntragen\ntrotzdem 1\ntrotzdem 2\ntrotzdem 3\ntun\ntUr\num 1\num 2\num 3\num 4\num zu sehen 1\num zu sehen 2\nund 1\nund 2\nunion\nuniversitAt\nunten\nunternehmen 1\nunternehmen 2\nunterstUtzen\nuntersuchen\nwachsen\nwald\nwarten\nwarum\nwas\nwasser\nvater\nwechseln 1\nwechseln 2\nweg\nwegen\nweil 1\nweil 2\nweiS\nweitermachen\nwelcher\nwelt\nwenig 1\nwenig 2\nweniger\nwenn\nwer\nvergangenheit\nverkaufen 1\nverkaufen 2\nverlassen 1\nverlassen 2\nverlieren\nverschwinden\nverstanden\nverstehen\nvertrag\nvertraut\nwetter\nwichtig\nwie 1\nwie 2\nwieder 1\nwieder 2\nwieder 3\nvielleicht 1\nvielleicht 2\nvier\nwind\nwirklich\nwissen\nwo 1\nwo 2\nwoche\nwohnung\nvoll\nvon dort\nvor\nvoraus\nvorherige 1\nvorherige 2\nvorne\nvorsitzende\nwort\nwovon\nwAhlen 1\nwAhlen 2\nwAhrend 1\nwAhrend 2\nwUnschen\nAuSern\nOffnen\nUber\nUberhaupt\n";
const int PROGMEM german_numbers[] =  { 486, 3, 157, 448, 426, 10, 397, 318, 171, 172, 74, 258, 457, 76, 303, 293, 398, 51, 13, 18, 121, 166, 235, 462, 460, 432, 434, 151, 230, 424, 368, 27, 484, 28, 298, 17, 38, 325, 483, 209, 12, 69, 305, 104, 128, 242, 91, 175, 344, 249, 260, 123, 456, 96, 19, 26, 280, 52, 299, 300, 131, 134, 218, 373, 197, 68, 309, 363, 163, 361, 114, 449, 451, 135, 406, 334, 366, 354, 405, 56, 21, 132, 23, 245, 24, 287, 352, 370, 331, 332, 146, 147, 369, 211, 279, 194, 495, 408, 458, 208, 286, 210, 290, 289, 110, 399, 122, 428, 410, 221, 246, 359, 204, 403, 106, 202, 82, 271, 292, 414, 54, 383, 35, 36, 232, 392, 8, 381, 323, 186, 333, 302, 9, 59, 191, 330, 266, 182, 181, 296, 256, 445, 478, 380, 459, 413, 469, 34, 92, 501, 20, 385, 244, 415, 461, 55, 276, 229, 295, 176, 170, 153, 219, 335, 475, 101, 431, 394, 178, 350, 144, 206, 133, 351, 270, 41, 401, 476, 63, 440, 346, 347, 489, 100, 306, 376, 326, 377, 62, 177, 217, 422, 137, 6, 396, 384, 429, 364, 365, 1, 200, 201, 387, 389, 173, 498, 203, 138, 168, 169, 470, 400, 317, 162, 371, 284, 378, 316, 73, 158, 90, 0, 49, 367, 125, 274, 263, 312, 264, 179, 411, 285, 187, 358, 482, 409, 301, 154, 127, 61, 254, 213, 339, 307, 188, 93, 43, 44, 436, 490, 105, 473, 491, 205, 468, 140, 485, 395, 97, 98, 243, 446, 222, 417, 251, 355, 223, 46, 341, 234, 425, 294, 72, 84, 228, 226, 481, 116, 433, 196, 64, 167, 70, 375, 45, 435, 390, 220, 474, 108, 109, 328, 30, 494, 193, 261, 336, 236, 174, 467, 477, 40, 443, 233, 37, 281, 463, 250, 450, 4, 5, 111, 160, 471, 185, 80, 142, 291, 338, 50, 99, 492, 314, 313, 66, 304, 320, 321, 319, 267, 329, 42, 488, 455, 487, 324, 15, 216, 465, 357, 502, 139, 322, 81, 345, 183, 215, 212, 95, 130, 136, 25, 348, 148, 237, 337, 83, 67, 308, 479, 275, 315, 88, 86, 277, 265, 259, 141, 87, 311, 360, 379, 349, 247, 248, 126, 102, 343, 65, 199, 71, 404, 356, 297, 11, 75, 207, 430, 412, 288, 180, 272, 2, 150, 421, 342, 407, 103, 115, 195, 416, 273, 388, 145, 149, 33, 118, 447, 327, 283, 189, 437, 374, 119, 16, 77, 78, 393, 438, 143, 340, 439, 500, 269, 444, 89, 262, 198, 499, 14, 58, 60, 402, 442, 124, 224, 282, 225, 231, 466, 85, 239, 240, 493, 423, 161, 362, 452, 112, 227, 214, 310, 464, 480, 156, 129, 252, 238, 253, 113, 184, 120, 117, 22, 241, 192, 418, 79, 278, 159, 255, 107, 386, 441, 39, 472, 257, 419, 420, 391, 155, 164, 268, 152, 427, 353, 48, 57, 31, 47, 32, 53, 382, 165, 453, 454, 7, 94, 372, 190, 29, 497, 496, };
const char PROGMEM russian[] = "\naBcoLKtho\nabGyct\nBahk\nBeZat\'\nBe3\nBeLYJ\nBecPLatho\nBoG\nBoLee\nBoL\'We 1\nBoL\'We 2\nBoL\'WoJ\nBoQt\'cQ\nBpat\' 1\nBpat\' 2\nBYbWIJ\nBYctpYJ\nBYt\'\nBYt\' Po3boLehhYm\nBYt\' poZDehhYm\nBYt\' cPocoBhYm 1\nBYt\' cPocoBhYm 2\nBYt\' ycLYWahhYm\nb 1\nb 2\nb 3\nb koheC\nb LKBom cLyTae\nb PpotIbhom cLyTae\nb cootbetctbII c\nb ctopohe\nb teTehIe 1\nb teTehIe 2\nb to Ze bpemQ\nbaZhYJ\nbbepx\nbepho 1\nbepho 2\nbetep\nbeTep\nbeTepIhka\nbeV\'\nbIDImo\nbLaDeLeC\nbLact\' 1\nbLact\' 2\nbmecte 1\nbmecte 2\nbmecto\nbhe\nbhe3aPho\nbhI3 1\nbhI3 2\nboDa\nboDIt\'\nboDIt\' maWIhy\nbo3\nbo3Dyx\nbo3moZho 1\nbo3moZho 2\nbo3moZhoct\'\nbo3moZhYJ\nbo3hIkat\'\nbo3pact\nbokpyG 1\nbokpyG 2\nboPpoc\nbPepeD\nbpemQ 1\nbpemQ 2\nbce 1\nbce 2\nbce eVe 1\nbce eVe 2\nbceGDa 1\nbceGDa 2\nbctabat\'\nbTepa\nbYBIpat\'\nbYBIpaQ\nbY3ob\nbYPoLhIt\'\nbYcokIJ\nGDe 1\nGDe 2\nGLa3\nGobopIt\'\nGoD\nGoLoba\nGoLoc\nGopoD\nGotobYJ\nGpahICa\nDa\nDabat\'\nDaZe\nDahhYe\nDba\nDbep\'\nDbIGat\'cQ\nDebyWka 1\nDebyWka 2\nDeJctbIe\nDeJctbIteL\'ho\nDeLat\' 1\nDeLat\' 2\nDeh\'\nDeh\'GI\nDepebo\nDecQt\'\nDLIhhYJ\nDLQ 1\nDLQ 2\nDo 1\nDo 2\nDo cIx Pop\nDoBabIt\'\nDoBabLQt\'\nDoboL\'ho\nDoGobop\nDom 1\nDom 2\nDoctatoTho\nDoctIGat\'\nDoT\'\nDpyG\nDpyGoJ 1\nDpyGoJ 2\nDymat\' 1\nDymat\' 2\nDyWa\nemy\necLI\nect\'\neVe 1\neVe 2\nZDat\'\nZeLat\'\nZI3h\'\nZIt\'\n3akoh\n3akpYto\n3ameTat\'\n3aPac\n3aPomhIt\' 1\n3aPomhIt\' 2\n3apPLata\n3atem\n3Dec\' 1\n3Dec\' 2\n3DopobYJ\n3emLQ\n3hat\'\n3haTIt\'\nI 1\nI 2\nIGpa\nIGpat\'\nIDtI\nI3GotobLehIe\nI3-3a\nI3mehIt\' 1\nI3mehIt\' 2\nILI 1\nILI 2\nImet\' mecto\nImQ\nIhoGDa\nIhtepec\nIckat\'\nIcPoL\'3obat\'\nIccLeDobat\'\nIctopIQ\nIcTe3hyt\'\nk 1\nk 2\nka3at\'cQ\nkak\nkak Eto\nkakoJ-to\nkak-to\nkaptIha\nkbaptIpa\nkLact\'\nkhIGa\nkoLITectbo\nkoLLekCIQ\nkomhata 1\nkomhata 2\nkomPahIQ 1\nkomPahIQ 2\nkoheC 1\nkoheC 2\nkoheTho\nkopaBL\'\nkopoha\nkotopYJ I3\nkpacIbYJ\nkpachYJ\nkto-to\nLec\nLeto\nLICo\nLobIt\'\nLyTWe\nLyTWee\nmaLeh\'kIJ\nmaL\'TIk\nmapWIpobat\'\nmat\'\nmaWIha\nmeZDy\nmeheDZep 1\nmeheDZep 2\nmeh\'We\nmecthYJ\nmecto\nmecQC\nmILLIoh\nmIp\nmhe\nmhoGo\nmoLoDoJ\nmoLoDoJ TeLobek\nmomeht\nmope\nmyZTIha\nmYcL\'\nha\nha camom DeLe\nhabephoe\nhaD\nhaDeQt\'cQ\nha3aD\nha3bat\'\nhaJtI\nhaPIcat\'\nhaPpImep\nhaPpQmyK\nhactoQVIJ\nhaCIQ\nhaTaLo\nhaTat\' 1\nhaTat\' 2\nheDeLQ\nhe3hakomeC\nhekotopYJ\nhemeDLehho\nhemhoGo 1\nhemhoGo 2\nheoBxoDImYJ\nheckoL\'ko\nhet 1\nhet 2\nhI\nhIZe\nho 1\nho 2\nho 3\nhobYJ\nhoGa\nhocIt\'\nhoT\'\nhpabItcQ\nhpabIt\'cQ\nhyZDat\'cQ 1\nhyZDat\'cQ 2\no 1\no 2\noBa\noB\"QchIt\'\noDIh\noDIhakobYJ\noDhako 1\noDhako 2\nokho\nokohTateL\'ho\noPpeDeLehho\noPpeDeLehhYJ\nochoba\nocoBehho\noctabat\'cQ\noctahobIt\'cQ\notbet\notbeTat\'\noteC\notkpYt\' 1\notkpYt\' 2\notkyDa\notmetIt\'\nothoWehIe\notPpabLQt\'\nottyDa\noCehka\noTeh\'\nPaDat\'\nPapa\nPapLameht\nPepbYJ\nPIc\'mo\nPLatIt\'\nPo meh\'WeJ mepe\nPoBeG\nPoBeZDat\'\nPobephyt\' 1\nPobephyt\' 2\nPoGoDa\nPoDapok 1\nPoDapok 2\nPoDDepZIbat\'\nPoDhImat\'\nPoDtbepZDat\'\nPo3aDI 1\nPo3aDI 2\nPo3boLQt\' 1\nPo3boLQt\' 2\nPo3Ze\nPoka3Ybat\'\nPokIhyt\' 1\nPokIhyt\' 2\nPokyPat\'\nPoLaGat\'\nPoLItITeckIJ\nPoLICIQ\nPoLhYJ\nPoLyTIt\'\nPomoGat\'\nPomoV\'\nPohImat\'\nPohQL\nPoPact\' b\nPocLe 1\nPocLe 2\nPocLe 3\nPocLe bceGo\nPocLeDhIJ\nPotomy Tto 1\nPotomy Tto 2\nPoTemy\nPoTtI\nPoQbIt\'cQ 1\nPoQbIt\'cQ 2\nPoQbIt\'cQ 3\nPpabIL\'hYJ\nPpabIteL\'ctbo\nPpeDLaGat\'\nPpeDceDateL\'\nPpeDctoQVIJ\nPpeDYDyVIJ\nPpe3IDeht\nPpem\'ep-mIhIctp\nPpIbYThYJ\nPpI3habat\'\nPpIhaDLeZat\'\nPpIhectI\nPpIhImat\' peWehIe\nPpIxoDIt\'\nPpITIha\nPpIWeL\nPpoBLema\nPpoDabat\' 1\nPpoDabat\' 2\nPpoDoLZat\'\nPpo3paThYJ\nPpoI3hectI\nPpocto\nPpotIb\nPpoFeccIQ\nPpoCeht\nPpoT\'\nPpoWLoe\nPpQmo ceJTac\nPyctoJ\nPQt\'\npaBota\npaBotat\'\npa3GobapIbat\'\npa3ym\npahee\npactI\npeBehok\npe3yL\'tat\npeWehIe\npoDIteL\'\npot\npyka\npycckIJ\npYhok\npQDom\npQDom c\nc\ncaDIt\'cQ\ncam\ncbQ3Ybat\'\nceGoDhQ\nceJTac\ncepDCe\ncItyaCIQ\ncka3at\'\ncLeDyKVIJ\ncLIWkom 1\ncLIWkom 2\ncLobo\ncLyTaJ\ncLyTat\'cQ\ncLyWat\'\ncLYWat\'\ncmept\'\ncmeQt\'cQ\ncmotpet\'\nchoba 1\nchoba 2\nchoba 3\ncoBaka\ncoBctbehhYJ\ncobet\ncobcem\nco3Dabat\'\ncooBVIt\'\ncotpyDhIk\ncoK3\ncPat\'\ncPepeDI\ncPIha\ncPpabIt\'cQ\ncPpaWIbat\' 1\ncPpaWIbat\' 2\nctapYJ\ncteha\nctoL\nctopoha\nctoQt\'\nctpaha\nctpoIt\'\ncyD\ncYh\ntak 1\ntak 2\ntakZe\ntakoJ\ntam 1\ntam 2\ntekyVIJ\nteLo\ntepQt\'\ntoL\'ko 1\ntoL\'ko 2\ntoTho 1\ntoTho 2\ntpahcLIpobat\'\ntpeBobat\' 1\ntpeBobat\' 2\ntpetIJ\ntpI\ntpyDhYJ\ntY\ntYcQTa\ntQhyt\'\nyDapIt\'\nyDaTa\nyZe\nyLICa\nyhIbepcItet\nycPokoIt\'cQ\nyctpoIt\'\nytpo\nyxoDIte\nyTIteL\'\nyTIt\'\nFIL\'m\nxoLoDhYJ\nxopoWIJ\nxopoWo\nxotet\'\nxotQ\nxpahIt\' 1\nxpahIt\' 2\nCeL\'\nCeha\nTac\nTact\'\nTeLobek\nTepe3\nTephYJ\nTetYpe\nTIctYJ\nTItat\'\nTLeh\nTto 1\nTto 2\nTto 3\nTtoBY ybIDet\' 1\nTtoBY ybIDet\' 2\nTto-hIByD\'\nTybctbo\nTybctbobat\'\nWaG\nWaGat\'\nWkoLa\nEctohckIJ 1\nEctohckIJ 2\nEtot\nQ3Yk\n";
const int PROGMEM russian_numbers[] = { 426, 27, 298, 93, 80, 452, 445, 100, 341, 46, 377, 376, 121, 467, 477, 47, 130, 275, 403, 385, 284, 378, 153, 104, 128, 371, 209, 78, 235, 110, 175, 7, 94, 347, 278, 498, 42, 488, 419, 486, 50, 24, 81, 280, 105, 473, 145, 149, 71, 484, 492, 15, 216, 466, 381, 8, 129, 485, 39, 472, 475, 474, 399, 76, 340, 500, 182, 30, 2, 150, 171, 172, 16, 77, 11, 162, 424, 41, 453, 454, 166, 428, 173, 155, 164, 368, 343, 0, 301, 75, 199, 456, 306, 90, 20, 87, 21, 118, 438, 197, 256, 435, 397, 420, 393, 395, 327, 335, 325, 180, 307, 34, 92, 48, 163, 361, 201, 200, 501, 192, 137, 217, 178, 106, 437, 380, 51, 398, 23, 245, 67, 400, 156, 383, 281, 463, 282, 372, 44, 43, 351, 133, 249, 10, 221, 246, 295, 366, 364, 365, 401, 213, 391, 425, 89, 262, 247, 248, 176, 396, 423, 239, 240, 122, 471, 404, 261, 446, 73, 288, 123, 442, 206, 117, 103, 316, 414, 159, 259, 230, 158, 309, 152, 297, 334, 72, 139, 342, 407, 58, 60, 208, 286, 236, 183, 154, 227, 83, 324, 125, 224, 379, 270, 333, 299, 300, 482, 312, 251, 45, 28, 447, 97, 98, 480, 140, 142, 167, 226, 214, 228, 294, 263, 264, 64, 223, 222, 244, 303, 394, 470, 497, 203, 388, 260, 191, 136, 273, 287, 329, 336, 13, 12, 18, 268, 478, 243, 141, 310, 464, 450, 234, 40, 233, 37, 14, 3, 157, 448, 443, 91, 119, 494, 220, 255, 449, 451, 143, 439, 242, 359, 495, 346, 49, 367, 9, 210, 132, 131, 17, 52, 114, 207, 462, 460, 85, 29, 185, 165, 250, 373, 345, 353, 68, 479, 186, 291, 338, 54, 135, 218, 481, 330, 476, 331, 332, 79, 55, 276, 402, 422, 134, 387, 389, 188, 204, 65, 272, 113, 184, 285, 440, 313, 314, 427, 344, 6, 1, 241, 22, 350, 108, 109, 328, 95, 468, 161, 362, 225, 302, 82, 271, 292, 487, 455, 293, 53, 57, 31, 319, 304, 418, 416, 170, 406, 290, 409, 326, 411, 320, 238, 253, 112, 358, 190, 194, 461, 19, 321, 493, 252, 317, 436, 469, 432, 434, 102, 219, 459, 124, 187, 410, 289, 458, 375, 177, 465, 417, 174, 193, 116, 88, 86, 363, 429, 274, 384, 277, 502, 111, 115, 195, 382, 323, 99, 168, 169, 374, 254, 308, 107, 386, 441, 138, 279, 267, 496, 202, 392, 433, 198, 215, 32, 357, 61, 181, 296, 457, 355, 189, 315, 356, 339, 38, 144, 311, 258, 265, 349, 360, 352, 370, 318, 127, 120, 4, 5, 101, 431, 63, 266, 483, 147, 146, 337, 369, 408, 421, 212, 489, 96, 430, 499, 26, 151, 70, 229, 490, 491, 59, 179, 62, 74, 390, 160, 69, 305, 33, 66, 412, 283, 84, 211, 237, 257, 322, 205, 196, 56, 231, 405, 269, 444, 232, 415, 413, 348, 25, 148, 35, 36, 354, 126, };

const int ESTONIAN_LEN = 3156;
const int ENGLISH_LEN = 3533;
const int GERMAN_LEN = 3990;
const int RUSSIAN_LEN = 4136;

char *language = estonian; // algab eesti keelest
int language_len = ESTONIAN_LEN;
int *language_numbers = estonian_numbers;

char *prev_language = estonian; // keelt vahetades võetakse enne olnud keel "prev"-keeleks, see on oluline tõlkimisel
int prev_len = ESTONIAN_LEN;
int *prev_numbers = estonian_numbers;

char ext_word[30] = "\0                           ";
int ext_word_len = 0;

int search = 1; // kas on otsingu- või tõlkerežiimis
int position = 0; // selle jaoks, kui sõna on pikem kui ekraan võimaldab, kui position on 1, siis on kõik 1 märgi võrra vasakule nihkunud

void addCharToStr(char letter, char string[], int *str_len) {
  string[*str_len] = letter;
  string[*str_len + 1] = '\0';
  (*str_len)++;
}

bool doesStrStartHere(char search_ar[], int search_ar_len, int index, const char word_list[]) {
  if (pgm_read_byte_near(word_list + index - 1) != '\n') {
    return 0;
  }
  for (int i = 0; i < search_ar_len; i++) {
    if (search_ar[i] != pgm_read_byte_near(word_list + i + index)) {
      return 0;
    }
  }
  return 1;
}

void alphFinder(char search_ar[], int search_ar_len, char given_ar[], int *given_len, const char word_list[], const int word_list_len) {
  int ext_index = 1;
  while (!doesStrStartHere(search_ar, search_ar_len, ext_index, word_list) && ext_index != word_list_len - 1) {
    //    Serial.println(ext_index);
    //    Serial.println(pgm_read_byte_near(word_list + ext_index));
    ext_index++;
  }
  if (ext_index == word_list_len - 1) {
    AllSegments(0);
    Display("**********", 10, 0);
    int y = 0;
    while (y == 0) {
      y = keyboard.getKey();
    }
    search = 1;
    given_ar[0] = '\0';
    *given_len = 0;
    return;
  }
  //  Serial.println(ext_index);
  //  Serial.println(pgm_read_byte_near(word_list + ext_index));
  given_ar[0] = '\0';
  (*given_len) = 0;
  for (int i = 0; pgm_read_byte_near(word_list + i + ext_index) != '\n'; i++) {
    (*given_len)++;
    given_ar[i] = pgm_read_byte_near(word_list + i + ext_index);
    given_ar[i + 1] = '\0';
  }
}


void makeNextWord(char current_word[], int *str_len, const char word_list[], const int word_list_len) {
  addCharToStr('\n', current_word, &*str_len);
  int ext_index = 1;

  while (!doesStrStartHere(current_word, *str_len, ext_index, word_list) && ext_index != word_list_len - 1) {
    //    Serial.println(ext_index);
    //    Serial.println(pgm_read_byte_near(word_list + ext_index));
    ext_index++;
  }

  while (pgm_read_byte_near(word_list + ext_index) != '\n') {
    ext_index++;
  }
  if (ext_index == word_list_len - 1) {
    ext_index--;

    while (pgm_read_byte_near(word_list + ext_index) != '\n') {
      ext_index--;
    }
  }
  ext_index++;
  current_word[0] = '\0';
  *str_len = 0;
  for (int i = 0; pgm_read_byte_near(word_list + i + ext_index) != '\n'; i++) {
    (*str_len)++;
    current_word[i] = pgm_read_byte_near(word_list + i + ext_index);
    current_word[i + 1] = '\0';
  }
}

void makeLastWord(char current_word[], int *str_len, const char word_list[], const int word_list_len) {
  addCharToStr('\n', current_word, &*str_len);
  int ext_index = 1;

  while (!doesStrStartHere(current_word, *str_len, ext_index, word_list) && ext_index != word_list_len - 1) {
    //    Serial.println(ext_index);
    //    Serial.println(pgm_read_byte_near(word_list + ext_index));
    ext_index++;
  }

  if (ext_index != 1) {
    ext_index--;
    while (pgm_read_byte_near(word_list + ext_index - 1) != '\n') {
      ext_index--;
    }
  }

  current_word[0] = '\0';
  *str_len = 0;
  for (int i = 0; pgm_read_byte_near(word_list + i + ext_index) != '\n'; i++) {
    (*str_len)++;
    current_word[i] = pgm_read_byte_near(word_list + i + ext_index);
    current_word[i + 1] = '\0';
  }
}

void translate(char current_word[], int *current_len, const char from_words[], const int from_len, const int from_numbers[], const char to_words[], const int to_len, const int to_numbers[]) {
  addCharToStr('\n', current_word, &*current_len);
  int ext_index = 1;
  int ext_word_index = 0;
  while (!doesStrStartHere(current_word, *current_len, ext_index, from_words) && ext_index != from_len - 1) {
    if (pgm_read_byte_near(from_words + ext_index) == '\n') {
      ext_word_index++;  //sõna 'index' from_words'is
     }
     ext_index++;
  }
  int ext_number = 0;
  if (from_words != estonian) {
    ext_number = pgm_read_word_near(from_numbers + ext_word_index);  //sõna 'indexile' vastav number ehk eestikeelse sõna 'index' estonian'is
  } else {
    ext_number = ext_word_index;
  }
  int ext_number_index = 0;
  if (to_words != estonian) {
    while (pgm_read_word_near(to_numbers + ext_number_index) != ext_number) {
      ext_number_index++;  //numbri "ext_number" index to_numbers'is, samuti vastava venekeelse sõna 'index' russian'is
    }
  } else {
    ext_number_index = ext_number;
  }
  ext_index = 1;
  ext_word_index = 0;
  while (ext_word_index != ext_number_index && ext_index != to_len - 1) {
    if (pgm_read_byte_near(to_words + ext_index) == '\n') {
      ext_word_index++;
    }
    ext_index++;  //saadud sõna algustähe index to_words'is
  }
  current_word[0] = '\0';
  *current_len = 0;
  for (int i = 0; pgm_read_byte_near(to_words + i + ext_index) != '\n'; i++) {
    (*current_len)++;
    current_word[i] = pgm_read_byte_near(to_words + i + ext_index);
    current_word[i + 1] = '\0';
  }
}

void setup() {
  delayMicroseconds(RESET_DELAY_USECS);
  pinMode(CS, OUTPUT);
  pinMode(WR, OUTPUT);
  pinMode(DATA, OUTPUT);
  HT162x_Command(CMD_SYS_EN);
  HT162x_Command(CMD_RC_INT);
  HT162x_Command(CMD_LCD_ON); // Should turn it back on
  AllElements(0);
}

void loop() {
  // put your main code here, to run repeatedly:
  static char x;
  x = 0;

  while (x == 0) {
    x = keyboard.getKey();
  }

  switch(x) {
    case 1 ... 34:
      if (search == 1) {
        if (ext_word_len >= 29) {
          ext_word[0] = '\0';
          ext_word_len = 0;
          position = 0;
          break;
        }
        if (language == russian) {
          addCharToStr(cyrillic[x - 1], ext_word, &ext_word_len);
        } else {
          addCharToStr(latin[x - 1], ext_word, &ext_word_len);
        }

        if (ext_word_len > 10) {
          position++;
        }
      }
    break;

    case 35:
      if (search == 0) {
        makeLastWord(ext_word, &ext_word_len, language, language_len);
        position = 0;
      }
    break;

    case 36:
      if (search == 0) {
        makeNextWord(ext_word, &ext_word_len, language, language_len);
        position = 0;
      }
    break;

    case 37:
      if (search == 0) {
        search = 1;
        ext_word[0] = '\0';
        ext_word_len = 0;
        position = 0;
      } else {
        alphFinder(ext_word, ext_word_len, ext_word, &ext_word_len, language, language_len);
        if (ext_word_len > 0) {
          search = 0;
        }
        position = 0;
      }
    break;

    case 38:
      if (search == 1) {
        if (position > 0) {
          position--;
        }
        if (ext_word_len > 0) {
          ext_word[ext_word_len - 1] = '\0';
          ext_word_len--;
        }
      } else {
        position = 0;
      }
    break;

    case 39:
      if (search == 0) {
        if (ext_word_len > position + 10) {
          position++;
        }
      }

    break;

    case 41 ... 44:
      prev_language = language;
      prev_len = language_len;
      prev_numbers = language_numbers;

      switch(x) {
        case 41:
          language = estonian;
          language_len = ESTONIAN_LEN;
          language_numbers = estonian_numbers;
        break;
        case 42:
          language = english;
          language_len = ENGLISH_LEN;
          language_numbers = english_numbers;
        break;
        case 43:
          language = russian;
          language_len = RUSSIAN_LEN;
          language_numbers = russian_numbers;
        break;
        case 44:
          language = german;
          language_len = GERMAN_LEN;
          language_numbers = german_numbers;
        break;
      }
      if (search == 0) { 
        translate(ext_word, &ext_word_len, prev_language, prev_len, prev_numbers, language, language_len, language_numbers);
        position = 0;
      } else {
        ext_word[0] = '\0';
        ext_word_len = 0;
        position = 0;
      }
    break;
  }

  AllSegments(0);
  Display(ext_word, ext_word_len, position);

}