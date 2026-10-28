/* QR Code Generator for JavaScript - Copyright (c) 2009 Kazuhiko Arase, MIT license (http://www.opensource.org/licenses/mit-license.php). */
var qrcode = function() {
var qrcode = function(typeNumber, errorCorrectionLevel) {
var PAD0 = 0xEC;
var PAD1 = 0x11;
var _typeNumber = typeNumber;
var _errorCorrectionLevel = QRErrorCorrectionLevel[errorCorrectionLevel];
var _modules = null;
var _moduleCount = 0;
var _dataCache = null;
var _dataList = [];
var _this = {};
var makeImpl = function(test, maskPattern) {
_moduleCount = _typeNumber * 4 + 17;
_modules = function(moduleCount) {
var modules = new Array(moduleCount);
for (var row = 0; row < moduleCount; row += 1) {
modules[row] = new Array(moduleCount);
for (var col = 0; col < moduleCount; col += 1) {
modules[row][col] = null;
}
}
return modules;
}(_moduleCount);
setupPositionProbePattern(0, 0);
setupPositionProbePattern(_moduleCount - 7, 0);
setupPositionProbePattern(0, _moduleCount - 7);
setupPositionAdjustPattern();
setupTimingPattern();
setupTypeInfo(test, maskPattern);
if (_typeNumber >= 7) {
setupTypeNumber(test);
}
if (_dataCache == null) {
_dataCache = createData(_typeNumber, _errorCorrectionLevel, _dataList);
}
mapData(_dataCache, maskPattern);
};
var setupPositionProbePattern = function(row, col) {
for (var r = -1; r <= 7; r += 1) {
if (row + r <= -1 || _moduleCount <= row + r) continue;
for (var c = -1; c <= 7; c += 1) {
if (col + c <= -1 || _moduleCount <= col + c) continue;
if ( (0 <= r && r <= 6 && (c == 0 || c == 6) )
|| (0 <= c && c <= 6 && (r == 0 || r == 6) )
|| (2 <= r && r <= 4 && 2 <= c && c <= 4) ) {
_modules[row + r][col + c] = true;
} else {
_modules[row + r][col + c] = false;
}
}
}
};
var getBestMaskPattern = function() {
var minLostPoint = 0;
var pattern = 0;
for (var i = 0; i < 8; i += 1) {
makeImpl(true, i);
var lostPoint = QRUtil.getLostPoint(_this);
if (i == 0 || minLostPoint > lostPoint) {
minLostPoint = lostPoint;
pattern = i;
}
}
return pattern;
};
var setupTimingPattern = function() {
for (var r = 8; r < _moduleCount - 8; r += 1) {
if (_modules[r][6] != null) {
continue;
}
_modules[r][6] = (r % 2 == 0);
}
for (var c = 8; c < _moduleCount - 8; c += 1) {
if (_modules[6][c] != null) {
continue;
}
_modules[6][c] = (c % 2 == 0);
}
};
var setupPositionAdjustPattern = function() {
var pos = QRUtil.getPatternPosition(_typeNumber);
for (var i = 0; i < pos.length; i += 1) {
for (var j = 0; j < pos.length; j += 1) {
var row = pos[i];
var col = pos[j];
if (_modules[row][col] != null) {
continue;
}
for (var r = -2; r <= 2; r += 1) {
for (var c = -2; c <= 2; c += 1) {
if (r == -2 || r == 2 || c == -2 || c == 2
|| (r == 0 && c == 0) ) {
_modules[row + r][col + c] = true;
} else {
_modules[row + r][col + c] = false;
}
}
}
}
}
};
var setupTypeNumber = function(test) {
var bits = QRUtil.getBCHTypeNumber(_typeNumber);
for (var i = 0; i < 18; i += 1) {
var mod = (!test && ( (bits >> i) & 1) == 1);
_modules[Math.floor(i / 3)][i % 3 + _moduleCount - 8 - 3] = mod;
}
for (var i = 0; i < 18; i += 1) {
var mod = (!test && ( (bits >> i) & 1) == 1);
_modules[i % 3 + _moduleCount - 8 - 3][Math.floor(i / 3)] = mod;
}
};
var setupTypeInfo = function(test, maskPattern) {
var data = (_errorCorrectionLevel << 3) | maskPattern;
var bits = QRUtil.getBCHTypeInfo(data);
for (var i = 0; i < 15; i += 1) {
var mod = (!test && ( (bits >> i) & 1) == 1);
if (i < 6) {
_modules[i][8] = mod;
} else if (i < 8) {
_modules[i + 1][8] = mod;
} else {
_modules[_moduleCount - 15 + i][8] = mod;
}
}
for (var i = 0; i < 15; i += 1) {
var mod = (!test && ( (bits >> i) & 1) == 1);
if (i < 8) {
_modules[8][_moduleCount - i - 1] = mod;
} else if (i < 9) {
_modules[8][15 - i - 1 + 1] = mod;
} else {
_modules[8][15 - i - 1] = mod;
}
}
_modules[_moduleCount - 8][8] = (!test);
};
var mapData = function(data, maskPattern) {
var inc = -1;
var row = _moduleCount - 1;
var bitIndex = 7;
var byteIndex = 0;
var maskFunc = QRUtil.getMaskFunction(maskPattern);
for (var col = _moduleCount - 1; col > 0; col -= 2) {
if (col == 6) col -= 1;
while (true) {
for (var c = 0; c < 2; c += 1) {
if (_modules[row][col - c] == null) {
var dark = false;
if (byteIndex < data.length) {
dark = ( ( (data[byteIndex] >>> bitIndex) & 1) == 1);
}
var mask = maskFunc(row, col - c);
if (mask) {
dark = !dark;
}
_modules[row][col - c] = dark;
bitIndex -= 1;
if (bitIndex == -1) {
byteIndex += 1;
bitIndex = 7;
}
}
}
row += inc;
if (row < 0 || _moduleCount <= row) {
row -= inc;
inc = -inc;
break;
}
}
}
};
var createBytes = function(buffer, rsBlocks) {
var offset = 0;
var maxDcCount = 0;
var maxEcCount = 0;
var dcdata = new Array(rsBlocks.length);
var ecdata = new Array(rsBlocks.length);
for (var r = 0; r < rsBlocks.length; r += 1) {
var dcCount = rsBlocks[r].dataCount;
var ecCount = rsBlocks[r].totalCount - dcCount;
maxDcCount = Math.max(maxDcCount, dcCount);
maxEcCount = Math.max(maxEcCount, ecCount);
dcdata[r] = new Array(dcCount);
for (var i = 0; i < dcdata[r].length; i += 1) {
dcdata[r][i] = 0xff & buffer.getBuffer()[i + offset];
}
offset += dcCount;
var rsPoly = QRUtil.getErrorCorrectPolynomial(ecCount);
var rawPoly = qrPolynomial(dcdata[r], rsPoly.getLength() - 1);
var modPoly = rawPoly.mod(rsPoly);
ecdata[r] = new Array(rsPoly.getLength() - 1);
for (var i = 0; i < ecdata[r].length; i += 1) {
var modIndex = i + modPoly.getLength() - ecdata[r].length;
ecdata[r][i] = (modIndex >= 0)? modPoly.getAt(modIndex) : 0;
}
}
var totalCodeCount = 0;
for (var i = 0; i < rsBlocks.length; i += 1) {
totalCodeCount += rsBlocks[i].totalCount;
}
var data = new Array(totalCodeCount);
var index = 0;
for (var i = 0; i < maxDcCount; i += 1) {
for (var r = 0; r < rsBlocks.length; r += 1) {
if (i < dcdata[r].length) {
data[index] = dcdata[r][i];
index += 1;
}
}
}
for (var i = 0; i < maxEcCount; i += 1) {
for (var r = 0; r < rsBlocks.length; r += 1) {
if (i < ecdata[r].length) {
data[index] = ecdata[r][i];
index += 1;
}
}
}
return data;
};
var createData = function(typeNumber, errorCorrectionLevel, dataList) {
var rsBlocks = QRRSBlock.getRSBlocks(typeNumber, errorCorrectionLevel);
var buffer = qrBitBuffer();
for (var i = 0; i < dataList.length; i += 1) {
var data = dataList[i];
buffer.put(data.getMode(), 4);
buffer.put(data.getLength(), QRUtil.getLengthInBits(data.getMode(), typeNumber) );
data.write(buffer);
}
var totalDataCount = 0;
for (var i = 0; i < rsBlocks.length; i += 1) {
totalDataCount += rsBlocks[i].dataCount;
}
if (buffer.getLengthInBits() > totalDataCount * 8) {
throw 'code length overflow. ('
+ buffer.getLengthInBits()
+ '>'
+ totalDataCount * 8
+ ')';
}
if (buffer.getLengthInBits() + 4 <= totalDataCount * 8) {
buffer.put(0, 4);
}
while (buffer.getLengthInBits() % 8 != 0) {
buffer.putBit(false);
}
while (true) {
if (buffer.getLengthInBits() >= totalDataCount * 8) {
break;
}
buffer.put(PAD0, 8);
if (buffer.getLengthInBits() >= totalDataCount * 8) {
break;
}
buffer.put(PAD1, 8);
}
return createBytes(buffer, rsBlocks);
};
_this.addData = function(data, mode) {
mode = mode || 'Byte';
var newData = null;
switch(mode) {
case 'Numeric' :
newData = qrNumber(data);
break;
case 'Alphanumeric' :
newData = qrAlphaNum(data);
break;
case 'Byte' :
newData = qr8BitByte(data);
break;
case 'Kanji' :
newData = qrKanji(data);
break;
default :
throw 'mode:' + mode;
}
_dataList.push(newData);
_dataCache = null;
};
_this.isDark = function(row, col) {
if (row < 0 || _moduleCount <= row || col < 0 || _moduleCount <= col) {
throw row + ',' + col;
}
return _modules[row][col];
};
_this.getModuleCount = function() {
return _moduleCount;
};
_this.make = function() {
if (_typeNumber < 1) {
var typeNumber = 1;
for (; typeNumber < 40; typeNumber++) {
var rsBlocks = QRRSBlock.getRSBlocks(typeNumber, _errorCorrectionLevel);
var buffer = qrBitBuffer();
for (var i = 0; i < _dataList.length; i++) {
var data = _dataList[i];
buffer.put(data.getMode(), 4);
buffer.put(data.getLength(), QRUtil.getLengthInBits(data.getMode(), typeNumber) );
data.write(buffer);
}
var totalDataCount = 0;
for (var i = 0; i < rsBlocks.length; i++) {
totalDataCount += rsBlocks[i].dataCount;
}
if (buffer.getLengthInBits() <= totalDataCount * 8) {
break;
}
}
_typeNumber = typeNumber;
}
makeImpl(false, getBestMaskPattern() );
};
_this.createTableTag = function(cellSize, margin) {
cellSize = cellSize || 2;
margin = (typeof margin == 'undefined')? cellSize * 4 : margin;
var qrHtml = '';
qrHtml += '<table style="';
qrHtml += ' border-width: 0px; border-style: none;';
qrHtml += ' border-collapse: collapse;';
qrHtml += ' padding: 0px; margin: ' + margin + 'px;';
qrHtml += '">';
qrHtml += '<tbody>';
for (var r = 0; r < _this.getModuleCount(); r += 1) {
qrHtml += '<tr>';
for (var c = 0; c < _this.getModuleCount(); c += 1) {
qrHtml += '<td style="';
qrHtml += ' border-width: 0px; border-style: none;';
qrHtml += ' border-collapse: collapse;';
qrHtml += ' padding: 0px; margin: 0px;';
qrHtml += ' width: ' + cellSize + 'px;';
qrHtml += ' height: ' + cellSize + 'px;';
qrHtml += ' background-color: ';
qrHtml += _this.isDark(r, c)? '#000000' : '#ffffff';
qrHtml += ';';
qrHtml += '"/>';
}
qrHtml += '</tr>';
}
qrHtml += '</tbody>';
qrHtml += '</table>';
return qrHtml;
};
_this.createSvgTag = function(cellSize, margin, alt, title) {
var opts = {};
if (typeof arguments[0] == 'object') {
opts = arguments[0];
cellSize = opts.cellSize;
margin = opts.margin;
alt = opts.alt;
title = opts.title;
}
cellSize = cellSize || 2;
margin = (typeof margin == 'undefined')? cellSize * 4 : margin;
alt = (typeof alt === 'string') ? {text: alt} : alt || {};
alt.text = alt.text || null;
alt.id = (alt.text) ? alt.id || 'qrcode-description' : null;
title = (typeof title === 'string') ? {text: title} : title || {};
title.text = title.text || null;
title.id = (title.text) ? title.id || 'qrcode-title' : null;
var size = _this.getModuleCount() * cellSize + margin * 2;
var c, mc, r, mr, qrSvg='', rect;
rect = 'l' + cellSize + ',0 0,' + cellSize +
' -' + cellSize + ',0 0,-' + cellSize + 'z ';
qrSvg += '<svg version="1.1" xmlns="http://www.w3.org/2000/svg"';
qrSvg += !opts.scalable ? ' width="' + size + 'px" height="' + size + 'px"' : '';
qrSvg += ' viewBox="0 0 ' + size + ' ' + size + '" ';
qrSvg += ' preserveAspectRatio="xMinYMin meet"';
qrSvg += (title.text || alt.text) ? ' role="img" aria-labelledby="' +
escapeXml([title.id, alt.id].join(' ').trim() ) + '"' : '';
qrSvg += '>';
qrSvg += (title.text) ? '<title id="' + escapeXml(title.id) + '">' +
escapeXml(title.text) + '</title>' : '';
qrSvg += (alt.text) ? '<description id="' + escapeXml(alt.id) + '">' +
escapeXml(alt.text) + '</description>' : '';
qrSvg += '<rect width="100%" height="100%" fill="white" cx="0" cy="0"/>';
qrSvg += '<path d="';
for (r = 0; r < _this.getModuleCount(); r += 1) {
mr = r * cellSize + margin;
for (c = 0; c < _this.getModuleCount(); c += 1) {
if (_this.isDark(r, c) ) {
mc = c*cellSize+margin;
qrSvg += 'M' + mc + ',' + mr + rect;
}
}
}
qrSvg += '" stroke="transparent" fill="black"/>';
qrSvg += '</svg>';
return qrSvg;
};
_this.createDataURL = function(cellSize, margin) {
cellSize = cellSize || 2;
margin = (typeof margin == 'undefined')? cellSize * 4 : margin;
var size = _this.getModuleCount() * cellSize + margin * 2;
var min = margin;
var max = size - margin;
return createDataURL(size, size, function(x, y) {
if (min <= x && x < max && min <= y && y < max) {
var c = Math.floor( (x - min) / cellSize);
var r = Math.floor( (y - min) / cellSize);
return _this.isDark(r, c)? 0 : 1;
} else {
return 1;
}
} );
};
_this.createImgTag = function(cellSize, margin, alt) {
cellSize = cellSize || 2;
margin = (typeof margin == 'undefined')? cellSize * 4 : margin;
var size = _this.getModuleCount() * cellSize + margin * 2;
var img = '';
img += '<img';
img += '\u0020src="';
img += _this.createDataURL(cellSize, margin);
img += '"';
img += '\u0020width="';
img += size;
img += '"';
img += '\u0020height="';
img += size;
img += '"';
if (alt) {
img += '\u0020alt="';
img += escapeXml(alt);
img += '"';
}
img += '/>';
return img;
};
var escapeXml = function(s) {
var escaped = '';
for (var i = 0; i < s.length; i += 1) {
var c = s.charAt(i);
switch(c) {
case '<': escaped += '&lt;'; break;
case '>': escaped += '&gt;'; break;
case '&': escaped += '&amp;'; break;
case '"': escaped += '&quot;'; break;
default : escaped += c; break;
}
}
return escaped;
};
var _createHalfASCII = function(margin) {
var cellSize = 1;
margin = (typeof margin == 'undefined')? cellSize * 2 : margin;
var size = _this.getModuleCount() * cellSize + margin * 2;
var min = margin;
var max = size - margin;
var y, x, r1, r2, p;
var blocks = {
'ââ': 'â',
'â ': 'â',
' â': 'â',
'  ': ' '
};
var blocksLastLineNoMargin = {
'ââ': 'â',
'â ': 'â',
' â': ' ',
'  ': ' '
};
var ascii = '';
for (y = 0; y < size; y += 2) {
r1 = Math.floor((y - min) / cellSize);
r2 = Math.floor((y + 1 - min) / cellSize);
for (x = 0; x < size; x += 1) {
p = 'â';
if (min <= x && x < max && min <= y && y < max && _this.isDark(r1, Math.floor((x - min) / cellSize))) {
p = ' ';
}
if (min <= x && x < max && min <= y+1 && y+1 < max && _this.isDark(r2, Math.floor((x - min) / cellSize))) {
p += ' ';
}
else {
p += 'â';
}
ascii += (margin < 1 && y+1 >= max) ? blocksLastLineNoMargin[p] : blocks[p];
}
ascii += '\n';
}
if (size % 2 && margin > 0) {
return ascii.substring(0, ascii.length - size - 1) + Array(size+1).join('â');
}
return ascii.substring(0, ascii.length-1);
};
_this.createASCII = function(cellSize, margin) {
cellSize = cellSize || 1;
if (cellSize < 2) {
return _createHalfASCII(margin);
}
cellSize -= 1;
margin = (typeof margin == 'undefined')? cellSize * 2 : margin;
var size = _this.getModuleCount() * cellSize + margin * 2;
var min = margin;
var max = size - margin;
var y, x, r, p;
var white = Array(cellSize+1).join('ââ');
var black = Array(cellSize+1).join('  ');
var ascii = '';
var line = '';
for (y = 0; y < size; y += 1) {
r = Math.floor( (y - min) / cellSize);
line = '';
for (x = 0; x < size; x += 1) {
p = 1;
if (min <= x && x < max && min <= y && y < max && _this.isDark(r, Math.floor((x - min) / cellSize))) {
p = 0;
}
line += p ? white : black;
}
for (r = 0; r < cellSize; r += 1) {
ascii += line + '\n';
}
}
return ascii.substring(0, ascii.length-1);
};
_this.renderTo2dContext = function(context, cellSize) {
cellSize = cellSize || 2;
var length = _this.getModuleCount();
for (var row = 0; row < length; row++) {
for (var col = 0; col < length; col++) {
context.fillStyle = _this.isDark(row, col) ? 'black' : 'white';
context.fillRect(col * cellSize, row * cellSize, cellSize, cellSize);
}
}
}
return _this;
};
qrcode.stringToBytesFuncs = {
'default' : function(s) {
var bytes = [];
for (var i = 0; i < s.length; i += 1) {
var c = s.charCodeAt(i);
bytes.push(c & 0xff);
}
return bytes;
}
};
qrcode.stringToBytes = qrcode.stringToBytesFuncs['default'];
qrcode.createStringToBytes = function(unicodeData, numChars) {
var unicodeMap = function() {
var bin = base64DecodeInputStream(unicodeData);
var read = function() {
var b = bin.read();
if (b == -1) throw 'eof';
return b;
};
var count = 0;
var unicodeMap = {};
while (true) {
var b0 = bin.read();
if (b0 == -1) break;
var b1 = read();
var b2 = read();
var b3 = read();
var k = String.fromCharCode( (b0 << 8) | b1);
var v = (b2 << 8) | b3;
unicodeMap[k] = v;
count += 1;
}
if (count != numChars) {
throw count + ' != ' + numChars;
}
return unicodeMap;
}();
var unknownChar = '?'.charCodeAt(0);
return function(s) {
var bytes = [];
for (var i = 0; i < s.length; i += 1) {
var c = s.charCodeAt(i);
if (c < 128) {
bytes.push(c);
} else {
var b = unicodeMap[s.charAt(i)];
if (typeof b == 'number') {
if ( (b & 0xff) == b) {
bytes.push(b);
} else {
bytes.push(b >>> 8);
bytes.push(b & 0xff);
}
} else {
bytes.push(unknownChar);
}
}
}
return bytes;
};
};
var QRMode = {
MODE_NUMBER :    1 << 0,
MODE_ALPHA_NUM : 1 << 1,
MODE_8BIT_BYTE : 1 << 2,
MODE_KANJI :     1 << 3
};
var QRErrorCorrectionLevel = {
L : 1,
M : 0,
Q : 3,
H : 2
};
var QRMaskPattern = {
PATTERN000 : 0,
PATTERN001 : 1,
PATTERN010 : 2,
PATTERN011 : 3,
PATTERN100 : 4,
PATTERN101 : 5,
PATTERN110 : 6,
PATTERN111 : 7
};
var QRUtil = function() {
var PATTERN_POSITION_TABLE = [
[],
[6, 18],
[6, 22],
[6, 26],
[6, 30],
[6, 34],
[6, 22, 38],
[6, 24, 42],
[6, 26, 46],
[6, 28, 50],
[6, 30, 54],
[6, 32, 58],
[6, 34, 62],
[6, 26, 46, 66],
[6, 26, 48, 70],
[6, 26, 50, 74],
[6, 30, 54, 78],
[6, 30, 56, 82],
[6, 30, 58, 86],
[6, 34, 62, 90],
[6, 28, 50, 72, 94],
[6, 26, 50, 74, 98],
[6, 30, 54, 78, 102],
[6, 28, 54, 80, 106],
[6, 32, 58, 84, 110],
[6, 30, 58, 86, 114],
[6, 34, 62, 90, 118],
[6, 26, 50, 74, 98, 122],
[6, 30, 54, 78, 102, 126],
[6, 26, 52, 78, 104, 130],
[6, 30, 56, 82, 108, 134],
[6, 34, 60, 86, 112, 138],
[6, 30, 58, 86, 114, 142],
[6, 34, 62, 90, 118, 146],
[6, 30, 54, 78, 102, 126, 150],
[6, 24, 50, 76, 102, 128, 154],
[6, 28, 54, 80, 106, 132, 158],
[6, 32, 58, 84, 110, 136, 162],
[6, 26, 54, 82, 110, 138, 166],
[6, 30, 58, 86, 114, 142, 170]
];
var G15 = (1 << 10) | (1 << 8) | (1 << 5) | (1 << 4) | (1 << 2) | (1 << 1) | (1 << 0);
var G18 = (1 << 12) | (1 << 11) | (1 << 10) | (1 << 9) | (1 << 8) | (1 << 5) | (1 << 2) | (1 << 0);
var G15_MASK = (1 << 14) | (1 << 12) | (1 << 10) | (1 << 4) | (1 << 1);
var _this = {};
var getBCHDigit = function(data) {
var digit = 0;
while (data != 0) {
digit += 1;
data >>>= 1;
}
return digit;
};
_this.getBCHTypeInfo = function(data) {
var d = data << 10;
while (getBCHDigit(d) - getBCHDigit(G15) >= 0) {
d ^= (G15 << (getBCHDigit(d) - getBCHDigit(G15) ) );
}
return ( (data << 10) | d) ^ G15_MASK;
};
_this.getBCHTypeNumber = function(data) {
var d = data << 12;
while (getBCHDigit(d) - getBCHDigit(G18) >= 0) {
d ^= (G18 << (getBCHDigit(d) - getBCHDigit(G18) ) );
}
return (data << 12) | d;
};
_this.getPatternPosition = function(typeNumber) {
return PATTERN_POSITION_TABLE[typeNumber - 1];
};
_this.getMaskFunction = function(maskPattern) {
switch (maskPattern) {
case QRMaskPattern.PATTERN000 :
return function(i, j) { return (i + j) % 2 == 0; };
case QRMaskPattern.PATTERN001 :
return function(i, j) { return i % 2 == 0; };
case QRMaskPattern.PATTERN010 :
return function(i, j) { return j % 3 == 0; };
case QRMaskPattern.PATTERN011 :
return function(i, j) { return (i + j) % 3 == 0; };
case QRMaskPattern.PATTERN100 :
return function(i, j) { return (Math.floor(i / 2) + Math.floor(j / 3) ) % 2 == 0; };
case QRMaskPattern.PATTERN101 :
return function(i, j) { return (i * j) % 2 + (i * j) % 3 == 0; };
case QRMaskPattern.PATTERN110 :
return function(i, j) { return ( (i * j) % 2 + (i * j) % 3) % 2 == 0; };
case QRMaskPattern.PATTERN111 :
return function(i, j) { return ( (i * j) % 3 + (i + j) % 2) % 2 == 0; };
default :
throw 'bad maskPattern:' + maskPattern;
}
};
_this.getErrorCorrectPolynomial = function(errorCorrectLength) {
var a = qrPolynomial([1], 0);
for (var i = 0; i < errorCorrectLength; i += 1) {
a = a.multiply(qrPolynomial([1, QRMath.gexp(i)], 0) );
}
return a;
};
_this.getLengthInBits = function(mode, type) {
if (1 <= type && type < 10) {
switch(mode) {
case QRMode.MODE_NUMBER    : return 10;
case QRMode.MODE_ALPHA_NUM : return 9;
case QRMode.MODE_8BIT_BYTE : return 8;
case QRMode.MODE_KANJI     : return 8;
default :
throw 'mode:' + mode;
}
} else if (type < 27) {
switch(mode) {
case QRMode.MODE_NUMBER    : return 12;
case QRMode.MODE_ALPHA_NUM : return 11;
case QRMode.MODE_8BIT_BYTE : return 16;
case QRMode.MODE_KANJI     : return 10;
default :
throw 'mode:' + mode;
}
} else if (type < 41) {
switch(mode) {
case QRMode.MODE_NUMBER    : return 14;
case QRMode.MODE_ALPHA_NUM : return 13;
case QRMode.MODE_8BIT_BYTE : return 16;
case QRMode.MODE_KANJI     : return 12;
default :
throw 'mode:' + mode;
}
} else {
throw 'type:' + type;
}
};
_this.getLostPoint = function(qrcode) {
var moduleCount = qrcode.getModuleCount();
var lostPoint = 0;
for (var row = 0; row < moduleCount; row += 1) {
for (var col = 0; col < moduleCount; col += 1) {
var sameCount = 0;
var dark = qrcode.isDark(row, col);
for (var r = -1; r <= 1; r += 1) {
if (row + r < 0 || moduleCount <= row + r) {
continue;
}
for (var c = -1; c <= 1; c += 1) {
if (col + c < 0 || moduleCount <= col + c) {
continue;
}
if (r == 0 && c == 0) {
continue;
}
if (dark == qrcode.isDark(row + r, col + c) ) {
sameCount += 1;
}
}
}
if (sameCount > 5) {
lostPoint += (3 + sameCount - 5);
}
}
};
for (var row = 0; row < moduleCount - 1; row += 1) {
for (var col = 0; col < moduleCount - 1; col += 1) {
var count = 0;
if (qrcode.isDark(row, col) ) count += 1;
if (qrcode.isDark(row + 1, col) ) count += 1;
if (qrcode.isDark(row, col + 1) ) count += 1;
if (qrcode.isDark(row + 1, col + 1) ) count += 1;
if (count == 0 || count == 4) {
lostPoint += 3;
}
}
}
for (var row = 0; row < moduleCount; row += 1) {
for (var col = 0; col < moduleCount - 6; col += 1) {
if (qrcode.isDark(row, col)
&& !qrcode.isDark(row, col + 1)
&&  qrcode.isDark(row, col + 2)
&&  qrcode.isDark(row, col + 3)
&&  qrcode.isDark(row, col + 4)
&& !qrcode.isDark(row, col + 5)
&&  qrcode.isDark(row, col + 6) ) {
lostPoint += 40;
}
}
}
for (var col = 0; col < moduleCount; col += 1) {
for (var row = 0; row < moduleCount - 6; row += 1) {
if (qrcode.isDark(row, col)
&& !qrcode.isDark(row + 1, col)
&&  qrcode.isDark(row + 2, col)
&&  qrcode.isDark(row + 3, col)
&&  qrcode.isDark(row + 4, col)
&& !qrcode.isDark(row + 5, col)
&&  qrcode.isDark(row + 6, col) ) {
lostPoint += 40;
}
}
}
var darkCount = 0;
for (var col = 0; col < moduleCount; col += 1) {
for (var row = 0; row < moduleCount; row += 1) {
if (qrcode.isDark(row, col) ) {
darkCount += 1;
}
}
}
var ratio = Math.abs(100 * darkCount / moduleCount / moduleCount - 50) / 5;
lostPoint += ratio * 10;
return lostPoint;
};
return _this;
}();
var QRMath = function() {
var EXP_TABLE = new Array(256);
var LOG_TABLE = new Array(256);
for (var i = 0; i < 8; i += 1) {
EXP_TABLE[i] = 1 << i;
}
for (var i = 8; i < 256; i += 1) {
EXP_TABLE[i] = EXP_TABLE[i - 4]
^ EXP_TABLE[i - 5]
^ EXP_TABLE[i - 6]
^ EXP_TABLE[i - 8];
}
for (var i = 0; i < 255; i += 1) {
LOG_TABLE[EXP_TABLE[i] ] = i;
}
var _this = {};
_this.glog = function(n) {
if (n < 1) {
throw 'glog(' + n + ')';
}
return LOG_TABLE[n];
};
_this.gexp = function(n) {
while (n < 0) {
n += 255;
}
while (n >= 256) {
n -= 255;
}
return EXP_TABLE[n];
};
return _this;
}();
function qrPolynomial(num, shift) {
if (typeof num.length == 'undefined') {
throw num.length + '/' + shift;
}
var _num = function() {
var offset = 0;
while (offset < num.length && num[offset] == 0) {
offset += 1;
}
var _num = new Array(num.length - offset + shift);
for (var i = 0; i < num.length - offset; i += 1) {
_num[i] = num[i + offset];
}
return _num;
}();
var _this = {};
_this.getAt = function(index) {
return _num[index];
};
_this.getLength = function() {
return _num.length;
};
_this.multiply = function(e) {
var num = new Array(_this.getLength() + e.getLength() - 1);
for (var i = 0; i < _this.getLength(); i += 1) {
for (var j = 0; j < e.getLength(); j += 1) {
num[i + j] ^= QRMath.gexp(QRMath.glog(_this.getAt(i) ) + QRMath.glog(e.getAt(j) ) );
}
}
return qrPolynomial(num, 0);
};
_this.mod = function(e) {
if (_this.getLength() - e.getLength() < 0) {
return _this;
}
var ratio = QRMath.glog(_this.getAt(0) ) - QRMath.glog(e.getAt(0) );
var num = new Array(_this.getLength() );
for (var i = 0; i < _this.getLength(); i += 1) {
num[i] = _this.getAt(i);
}
for (var i = 0; i < e.getLength(); i += 1) {
num[i] ^= QRMath.gexp(QRMath.glog(e.getAt(i) ) + ratio);
}
return qrPolynomial(num, 0).mod(e);
};
return _this;
};
var QRRSBlock = function() {
var RS_BLOCK_TABLE = [
[1, 26, 19],
[1, 26, 16],
[1, 26, 13],
[1, 26, 9],
[1, 44, 34],
[1, 44, 28],
[1, 44, 22],
[1, 44, 16],
[1, 70, 55],
[1, 70, 44],
[2, 35, 17],
[2, 35, 13],
[1, 100, 80],
[2, 50, 32],
[2, 50, 24],
[4, 25, 9],
[1, 134, 108],
[2, 67, 43],
[2, 33, 15, 2, 34, 16],
[2, 33, 11, 2, 34, 12],
[2, 86, 68],
[4, 43, 27],
[4, 43, 19],
[4, 43, 15],
[2, 98, 78],
[4, 49, 31],
[2, 32, 14, 4, 33, 15],
[4, 39, 13, 1, 40, 14],
[2, 121, 97],
[2, 60, 38, 2, 61, 39],
[4, 40, 18, 2, 41, 19],
[4, 40, 14, 2, 41, 15],
[2, 146, 116],
[3, 58, 36, 2, 59, 37],
[4, 36, 16, 4, 37, 17],
[4, 36, 12, 4, 37, 13],
[2, 86, 68, 2, 87, 69],
[4, 69, 43, 1, 70, 44],
[6, 43, 19, 2, 44, 20],
[6, 43, 15, 2, 44, 16],
[4, 101, 81],
[1, 80, 50, 4, 81, 51],
[4, 50, 22, 4, 51, 23],
[3, 36, 12, 8, 37, 13],
[2, 116, 92, 2, 117, 93],
[6, 58, 36, 2, 59, 37],
[4, 46, 20, 6, 47, 21],
[7, 42, 14, 4, 43, 15],
[4, 133, 107],
[8, 59, 37, 1, 60, 38],
[8, 44, 20, 4, 45, 21],
[12, 33, 11, 4, 34, 12],
[3, 145, 115, 1, 146, 116],
[4, 64, 40, 5, 65, 41],
[11, 36, 16, 5, 37, 17],
[11, 36, 12, 5, 37, 13],
[5, 109, 87, 1, 110, 88],
[5, 65, 41, 5, 66, 42],
[5, 54, 24, 7, 55, 25],
[11, 36, 12, 7, 37, 13],
[5, 122, 98, 1, 123, 99],
[7, 73, 45, 3, 74, 46],
[15, 43, 19, 2, 44, 20],
[3, 45, 15, 13, 46, 16],
[1, 135, 107, 5, 136, 108],
[10, 74, 46, 1, 75, 47],
[1, 50, 22, 15, 51, 23],
[2, 42, 14, 17, 43, 15],
[5, 150, 120, 1, 151, 121],
[9, 69, 43, 4, 70, 44],
[17, 50, 22, 1, 51, 23],
[2, 42, 14, 19, 43, 15],
[3, 141, 113, 4, 142, 114],
[3, 70, 44, 11, 71, 45],
[17, 47, 21, 4, 48, 22],
[9, 39, 13, 16, 40, 14],
[3, 135, 107, 5, 136, 108],
[3, 67, 41, 13, 68, 42],
[15, 54, 24, 5, 55, 25],
[15, 43, 15, 10, 44, 16],
[4, 144, 116, 4, 145, 117],
[17, 68, 42],
[17, 50, 22, 6, 51, 23],
[19, 46, 16, 6, 47, 17],
[2, 139, 111, 7, 140, 112],
[17, 74, 46],
[7, 54, 24, 16, 55, 25],
[34, 37, 13],
[4, 151, 121, 5, 152, 122],
[4, 75, 47, 14, 76, 48],
[11, 54, 24, 14, 55, 25],
[16, 45, 15, 14, 46, 16],
[6, 147, 117, 4, 148, 118],
[6, 73, 45, 14, 74, 46],
[11, 54, 24, 16, 55, 25],
[30, 46, 16, 2, 47, 17],
[8, 132, 106, 4, 133, 107],
[8, 75, 47, 13, 76, 48],
[7, 54, 24, 22, 55, 25],
[22, 45, 15, 13, 46, 16],
[10, 142, 114, 2, 143, 115],
[19, 74, 46, 4, 75, 47],
[28, 50, 22, 6, 51, 23],
[33, 46, 16, 4, 47, 17],
[8, 152, 122, 4, 153, 123],
[22, 73, 45, 3, 74, 46],
[8, 53, 23, 26, 54, 24],
[12, 45, 15, 28, 46, 16],
[3, 147, 117, 10, 148, 118],
[3, 73, 45, 23, 74, 46],
[4, 54, 24, 31, 55, 25],
[11, 45, 15, 31, 46, 16],
[7, 146, 116, 7, 147, 117],
[21, 73, 45, 7, 74, 46],
[1, 53, 23, 37, 54, 24],
[19, 45, 15, 26, 46, 16],
[5, 145, 115, 10, 146, 116],
[19, 75, 47, 10, 76, 48],
[15, 54, 24, 25, 55, 25],
[23, 45, 15, 25, 46, 16],
[13, 145, 115, 3, 146, 116],
[2, 74, 46, 29, 75, 47],
[42, 54, 24, 1, 55, 25],
[23, 45, 15, 28, 46, 16],
[17, 145, 115],
[10, 74, 46, 23, 75, 47],
[10, 54, 24, 35, 55, 25],
[19, 45, 15, 35, 46, 16],
[17, 145, 115, 1, 146, 116],
[14, 74, 46, 21, 75, 47],
[29, 54, 24, 19, 55, 25],
[11, 45, 15, 46, 46, 16],
[13, 145, 115, 6, 146, 116],
[14, 74, 46, 23, 75, 47],
[44, 54, 24, 7, 55, 25],
[59, 46, 16, 1, 47, 17],
[12, 151, 121, 7, 152, 122],
[12, 75, 47, 26, 76, 48],
[39, 54, 24, 14, 55, 25],
[22, 45, 15, 41, 46, 16],
[6, 151, 121, 14, 152, 122],
[6, 75, 47, 34, 76, 48],
[46, 54, 24, 10, 55, 25],
[2, 45, 15, 64, 46, 16],
[17, 152, 122, 4, 153, 123],
[29, 74, 46, 14, 75, 47],
[49, 54, 24, 10, 55, 25],
[24, 45, 15, 46, 46, 16],
[4, 152, 122, 18, 153, 123],
[13, 74, 46, 32, 75, 47],
[48, 54, 24, 14, 55, 25],
[42, 45, 15, 32, 46, 16],
[20, 147, 117, 4, 148, 118],
[40, 75, 47, 7, 76, 48],
[43, 54, 24, 22, 55, 25],
[10, 45, 15, 67, 46, 16],
[19, 148, 118, 6, 149, 119],
[18, 75, 47, 31, 76, 48],
[34, 54, 24, 34, 55, 25],
[20, 45, 15, 61, 46, 16]
];
var qrRSBlock = function(totalCount, dataCount) {
var _this = {};
_this.totalCount = totalCount;
_this.dataCount = dataCount;
return _this;
};
var _this = {};
var getRsBlockTable = function(typeNumber, errorCorrectionLevel) {
switch(errorCorrectionLevel) {
case QRErrorCorrectionLevel.L :
return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 0];
case QRErrorCorrectionLevel.M :
return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 1];
case QRErrorCorrectionLevel.Q :
return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 2];
case QRErrorCorrectionLevel.H :
return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 3];
default :
return undefined;
}
};
_this.getRSBlocks = function(typeNumber, errorCorrectionLevel) {
var rsBlock = getRsBlockTable(typeNumber, errorCorrectionLevel);
if (typeof rsBlock == 'undefined') {
throw 'bad rs block @ typeNumber:' + typeNumber +
'/errorCorrectionLevel:' + errorCorrectionLevel;
}
var length = rsBlock.length / 3;
var list = [];
for (var i = 0; i < length; i += 1) {
var count = rsBlock[i * 3 + 0];
var totalCount = rsBlock[i * 3 + 1];
var dataCount = rsBlock[i * 3 + 2];
for (var j = 0; j < count; j += 1) {
list.push(qrRSBlock(totalCount, dataCount) );
}
}
return list;
};
return _this;
}();
var qrBitBuffer = function() {
var _buffer = [];
var _length = 0;
var _this = {};
_this.getBuffer = function() {
return _buffer;
};
_this.getAt = function(index) {
var bufIndex = Math.floor(index / 8);
return ( (_buffer[bufIndex] >>> (7 - index % 8) ) & 1) == 1;
};
_this.put = function(num, length) {
for (var i = 0; i < length; i += 1) {
_this.putBit( ( (num >>> (length - i - 1) ) & 1) == 1);
}
};
_this.getLengthInBits = function() {
return _length;
};
_this.putBit = function(bit) {
var bufIndex = Math.floor(_length / 8);
if (_buffer.length <= bufIndex) {
_buffer.push(0);
}
if (bit) {
_buffer[bufIndex] |= (0x80 >>> (_length % 8) );
}
_length += 1;
};
return _this;
};
var qrNumber = function(data) {
var _mode = QRMode.MODE_NUMBER;
var _data = data;
var _this = {};
_this.getMode = function() {
return _mode;
};
_this.getLength = function(buffer) {
return _data.length;
};
_this.write = function(buffer) {
var data = _data;
var i = 0;
while (i + 2 < data.length) {
buffer.put(strToNum(data.substring(i, i + 3) ), 10);
i += 3;
}
if (i < data.length) {
if (data.length - i == 1) {
buffer.put(strToNum(data.substring(i, i + 1) ), 4);
} else if (data.length - i == 2) {
buffer.put(strToNum(data.substring(i, i + 2) ), 7);
}
}
};
var strToNum = function(s) {
var num = 0;
for (var i = 0; i < s.length; i += 1) {
num = num * 10 + chatToNum(s.charAt(i) );
}
return num;
};
var chatToNum = function(c) {
if ('0' <= c && c <= '9') {
return c.charCodeAt(0) - '0'.charCodeAt(0);
}
throw 'illegal char :' + c;
};
return _this;
};
var qrAlphaNum = function(data) {
var _mode = QRMode.MODE_ALPHA_NUM;
var _data = data;
var _this = {};
_this.getMode = function() {
return _mode;
};
_this.getLength = function(buffer) {
return _data.length;
};
_this.write = function(buffer) {
var s = _data;
var i = 0;
while (i + 1 < s.length) {
buffer.put(
getCode(s.charAt(i) ) * 45 +
getCode(s.charAt(i + 1) ), 11);
i += 2;
}
if (i < s.length) {
buffer.put(getCode(s.charAt(i) ), 6);
}
};
var getCode = function(c) {
if ('0' <= c && c <= '9') {
return c.charCodeAt(0) - '0'.charCodeAt(0);
} else if ('A' <= c && c <= 'Z') {
return c.charCodeAt(0) - 'A'.charCodeAt(0) + 10;
} else {
switch (c) {
case ' ' : return 36;
case '$' : return 37;
case '%' : return 38;
case '*' : return 39;
case '+' : return 40;
case '-' : return 41;
case '.' : return 42;
case '/' : return 43;
case ':' : return 44;
default :
throw 'illegal char :' + c;
}
}
};
return _this;
};
var qr8BitByte = function(data) {
var _mode = QRMode.MODE_8BIT_BYTE;
var _data = data;
var _bytes = qrcode.stringToBytes(data);
var _this = {};
_this.getMode = function() {
return _mode;
};
_this.getLength = function(buffer) {
return _bytes.length;
};
_this.write = function(buffer) {
for (var i = 0; i < _bytes.length; i += 1) {
buffer.put(_bytes[i], 8);
}
};
return _this;
};
var qrKanji = function(data) {
var _mode = QRMode.MODE_KANJI;
var _data = data;
var stringToBytes = qrcode.stringToBytesFuncs['SJIS'];
if (!stringToBytes) {
throw 'sjis not supported.';
}
!function(c, code) {
var test = stringToBytes(c);
if (test.length != 2 || ( (test[0] << 8) | test[1]) != code) {
throw 'sjis not supported.';
}
}('\u53cb', 0x9746);
var _bytes = stringToBytes(data);
var _this = {};
_this.getMode = function() {
return _mode;
};
_this.getLength = function(buffer) {
return ~~(_bytes.length / 2);
};
_this.write = function(buffer) {
var data = _bytes;
var i = 0;
while (i + 1 < data.length) {
var c = ( (0xff & data[i]) << 8) | (0xff & data[i + 1]);
if (0x8140 <= c && c <= 0x9FFC) {
c -= 0x8140;
} else if (0xE040 <= c && c <= 0xEBBF) {
c -= 0xC140;
} else {
throw 'illegal char at ' + (i + 1) + '/' + c;
}
c = ( (c >>> 8) & 0xff) * 0xC0 + (c & 0xff);
buffer.put(c, 13);
i += 2;
}
if (i < data.length) {
throw 'illegal char at ' + (i + 1);
}
};
return _this;
};
var byteArrayOutputStream = function() {
var _bytes = [];
var _this = {};
_this.writeByte = function(b) {
_bytes.push(b & 0xff);
};
_this.writeShort = function(i) {
_this.writeByte(i);
_this.writeByte(i >>> 8);
};
_this.writeBytes = function(b, off, len) {
off = off || 0;
len = len || b.length;
for (var i = 0; i < len; i += 1) {
_this.writeByte(b[i + off]);
}
};
_this.writeString = function(s) {
for (var i = 0; i < s.length; i += 1) {
_this.writeByte(s.charCodeAt(i) );
}
};
_this.toByteArray = function() {
return _bytes;
};
_this.toString = function() {
var s = '';
s += '[';
for (var i = 0; i < _bytes.length; i += 1) {
if (i > 0) {
s += ',';
}
s += _bytes[i];
}
s += ']';
return s;
};
return _this;
};
var base64EncodeOutputStream = function() {
var _buffer = 0;
var _buflen = 0;
var _length = 0;
var _base64 = '';
var _this = {};
var writeEncoded = function(b) {
_base64 += String.fromCharCode(encode(b & 0x3f) );
};
var encode = function(n) {
if (n < 0) {
} else if (n < 26) {
return 0x41 + n;
} else if (n < 52) {
return 0x61 + (n - 26);
} else if (n < 62) {
return 0x30 + (n - 52);
} else if (n == 62) {
return 0x2b;
} else if (n == 63) {
return 0x2f;
}
throw 'n:' + n;
};
_this.writeByte = function(n) {
_buffer = (_buffer << 8) | (n & 0xff);
_buflen += 8;
_length += 1;
while (_buflen >= 6) {
writeEncoded(_buffer >>> (_buflen - 6) );
_buflen -= 6;
}
};
_this.flush = function() {
if (_buflen > 0) {
writeEncoded(_buffer << (6 - _buflen) );
_buffer = 0;
_buflen = 0;
}
if (_length % 3 != 0) {
var padlen = 3 - _length % 3;
for (var i = 0; i < padlen; i += 1) {
_base64 += '=';
}
}
};
_this.toString = function() {
return _base64;
};
return _this;
};
var base64DecodeInputStream = function(str) {
var _str = str;
var _pos = 0;
var _buffer = 0;
var _buflen = 0;
var _this = {};
_this.read = function() {
while (_buflen < 8) {
if (_pos >= _str.length) {
if (_buflen == 0) {
return -1;
}
throw 'unexpected end of file./' + _buflen;
}
var c = _str.charAt(_pos);
_pos += 1;
if (c == '=') {
_buflen = 0;
return -1;
} else if (c.match(/^\s$/) ) {
continue;
}
_buffer = (_buffer << 6) | decode(c.charCodeAt(0) );
_buflen += 6;
}
var n = (_buffer >>> (_buflen - 8) ) & 0xff;
_buflen -= 8;
return n;
};
var decode = function(c) {
if (0x41 <= c && c <= 0x5a) {
return c - 0x41;
} else if (0x61 <= c && c <= 0x7a) {
return c - 0x61 + 26;
} else if (0x30 <= c && c <= 0x39) {
return c - 0x30 + 52;
} else if (c == 0x2b) {
return 62;
} else if (c == 0x2f) {
return 63;
} else {
throw 'c:' + c;
}
};
return _this;
};
var gifImage = function(width, height) {
var _width = width;
var _height = height;
var _data = new Array(width * height);
var _this = {};
_this.setPixel = function(x, y, pixel) {
_data[y * _width + x] = pixel;
};
_this.write = function(out) {
out.writeString('GIF87a');
out.writeShort(_width);
out.writeShort(_height);
out.writeByte(0x80); // 2bit
out.writeByte(0);
out.writeByte(0);
out.writeByte(0x00);
out.writeByte(0x00);
out.writeByte(0x00);
out.writeByte(0xff);
out.writeByte(0xff);
out.writeByte(0xff);
out.writeString(',');
out.writeShort(0);
out.writeShort(0);
out.writeShort(_width);
out.writeShort(_height);
out.writeByte(0);
var lzwMinCodeSize = 2;
var raster = getLZWRaster(lzwMinCodeSize);
out.writeByte(lzwMinCodeSize);
var offset = 0;
while (raster.length - offset > 255) {
out.writeByte(255);
out.writeBytes(raster, offset, 255);
offset += 255;
}
out.writeByte(raster.length - offset);
out.writeBytes(raster, offset, raster.length - offset);
out.writeByte(0x00);
out.writeString(';');
};
var bitOutputStream = function(out) {
var _out = out;
var _bitLength = 0;
var _bitBuffer = 0;
var _this = {};
_this.write = function(data, length) {
if ( (data >>> length) != 0) {
throw 'length over';
}
while (_bitLength + length >= 8) {
_out.writeByte(0xff & ( (data << _bitLength) | _bitBuffer) );
length -= (8 - _bitLength);
data >>>= (8 - _bitLength);
_bitBuffer = 0;
_bitLength = 0;
}
_bitBuffer = (data << _bitLength) | _bitBuffer;
_bitLength = _bitLength + length;
};
_this.flush = function() {
if (_bitLength > 0) {
_out.writeByte(_bitBuffer);
}
};
return _this;
};
var getLZWRaster = function(lzwMinCodeSize) {
var clearCode = 1 << lzwMinCodeSize;
var endCode = (1 << lzwMinCodeSize) + 1;
var bitLength = lzwMinCodeSize + 1;
var table = lzwTable();
for (var i = 0; i < clearCode; i += 1) {
table.add(String.fromCharCode(i) );
}
table.add(String.fromCharCode(clearCode) );
table.add(String.fromCharCode(endCode) );
var byteOut = byteArrayOutputStream();
var bitOut = bitOutputStream(byteOut);
bitOut.write(clearCode, bitLength);
var dataIndex = 0;
var s = String.fromCharCode(_data[dataIndex]);
dataIndex += 1;
while (dataIndex < _data.length) {
var c = String.fromCharCode(_data[dataIndex]);
dataIndex += 1;
if (table.contains(s + c) ) {
s = s + c;
} else {
bitOut.write(table.indexOf(s), bitLength);
if (table.size() < 0xfff) {
if (table.size() == (1 << bitLength) ) {
bitLength += 1;
}
table.add(s + c);
}
s = c;
}
}
bitOut.write(table.indexOf(s), bitLength);
bitOut.write(endCode, bitLength);
bitOut.flush();
return byteOut.toByteArray();
};
var lzwTable = function() {
var _map = {};
var _size = 0;
var _this = {};
_this.add = function(key) {
if (_this.contains(key) ) {
throw 'dup key:' + key;
}
_map[key] = _size;
_size += 1;
};
_this.size = function() {
return _size;
};
_this.indexOf = function(key) {
return _map[key];
};
_this.contains = function(key) {
return typeof _map[key] != 'undefined';
};
return _this;
};
return _this;
};
var createDataURL = function(width, height, getPixel) {
var gif = gifImage(width, height);
for (var y = 0; y < height; y += 1) {
for (var x = 0; x < width; x += 1) {
gif.setPixel(x, y, getPixel(x, y) );
}
}
var b = byteArrayOutputStream();
gif.write(b);
var base64 = base64EncodeOutputStream();
var bytes = b.toByteArray();
for (var i = 0; i < bytes.length; i += 1) {
base64.writeByte(bytes[i]);
}
base64.flush();
return 'data:image/gif;base64,' + base64;
};
return qrcode;
}();
!function() {
qrcode.stringToBytesFuncs['UTF-8'] = function(s) {
function toUTF8Array(str) {
var utf8 = [];
for (var i=0; i < str.length; i++) {
var charcode = str.charCodeAt(i);
if (charcode < 0x80) utf8.push(charcode);
else if (charcode < 0x800) {
utf8.push(0xc0 | (charcode >> 6),
0x80 | (charcode & 0x3f));
}
else if (charcode < 0xd800 || charcode >= 0xe000) {
utf8.push(0xe0 | (charcode >> 12),
0x80 | ((charcode>>6) & 0x3f),
0x80 | (charcode & 0x3f));
}
else {
i++;
charcode = 0x10000 + (((charcode & 0x3ff)<<10)
| (str.charCodeAt(i) & 0x3ff));
utf8.push(0xf0 | (charcode >>18),
0x80 | ((charcode>>12) & 0x3f),
0x80 | ((charcode>>6) & 0x3f),
0x80 | (charcode & 0x3f));
}
}
return utf8;
}
return toUTF8Array(s);
};
}();
var CR_I18N = {"keys":["A cheat crashed the game last time.","About","Action","Actions","Add a file","Add cheats","Add to favorites","Adds a â CheatRunner entry to each game's options menu, and a button combo that opens the trainer for the running game.","Advanced","All","All contributions will go toward our goal of acquiring a low-firmware PS5 (1.xxâ4.xx) for research, development, and future upgrades.","All downloads failed","All games","Also update files that changed","Applied","Apply","Apps","Auto-select","Back","Browse your library","Buttons","Buy Me a Coffee","Cheat","Cheat engine","CheatRunner - Cheat Trainer for your PS5!","Cheats","Cheats are held back this long after a game launches.","Cheats first","Choose a network","Circle","Clear filters","Clear flag","Clear search","Close","Collapse","Collapse menu","Color scheme of this dashboard.","Community","Community cheat repositories","Controller shortcut and game menu","Copy","Copy URL","Copy all","Copy cheats folder","Copy link","Crash detection","Crashed the game last time","Cross","Crypto","Crypto support","Dark","Delete","Disable all","Down","Download all sources","Download community cheats","Download failed","Download ready-made cheat files for your games. They are saved to /data/cheatrunner/cheats, and downloads keep running in the background.","Download selected","Downloaded","Drop a .mc4, .shn or .json file here, or click to browse","Enable controller shortcut","Enable shortcut","Engine on","Enter open","Errors only","Exact","Failed","Favorites","Files","Filter","Filter logs","Flag a cheat if the game closes right after it was turned on.","For users. By users.","Force apply","Games","Games with cheats","Games with patches","Gap between cheats (ms)","General","Held together to trigger the shortcut.","Hold time (ms)","Hold two buttons together to open the trainer for the running game.","Home-screen tile","Hooks into the system UI. Off by default - on some firmwares this can crash the home screen.","How long to hold the combo before it triggers.","How much detail is written to the log.","If CheatRunner was useful to you, consider supporting the project development! ð«¶","In use","Install the CheatRunner tile on the PS5 home screen at boot.","Join the Discord","Ko-Fi, coffee, crypto, PIX","Language","Language of this dashboard.","Last played","Left","Light","Live health of the cheat engine and memory patching.","Local cheat files","Log level","Log level filter","Logs","Main","Master switch for applying cheats and patches.","Master-code fixup","May not match this game version","Memory patching","Midnight","Minimum time between two cheat toggles.","Name A-Z","Name Z-A","No cheat file for this game yet.","No favorites yet. Tap the star on a game to add it.","Not running","On","Open on your phone","Open on your phone (QR code)","Open the dashboard on your phone","Open trainer","Options","Other version","Page","Patches","Play","QR code","QR code for the dashboard address","Re-apply the cheats that are ON every time this game launches","Ready","Recent","Refresh","Refresh game list","Relaunch","Report an issue","Rescan installed games","Reset to defaults","Resolve master-code dependent cheats automatically.","Restore","Right","Running","Running now","Save autoload","Scan this with your phone camera. Your phone must be on the same Wi-Fi as the PS5.","Search","Search by name or title ID","Search games","Search games, pages and actions","Select an address to copy it in full.","Settings","Show QR code","Shut down payload","Sort games","Source","Sources","Speed","Square","Start the game","Start this game to apply patches.","Status","Status and logs","Support CheatRunner","Support from Brazil","Support this project","Supporters","System","The essentials - theme and the cheat engine switch.","Theme","Theme and cheat engine","Time left","Time played","Title ID","Toggles","TouchPad","Trainer","Triangle","Up","Up to date","Use","Version","Wait after game start (ms)","Warnings and errors","What CheatRunner has been doing - useful when something does not work.","With cheats","ââ navigate","All settings go back to their defaults. The web port is kept.","Another cheat is being applied. Try again in a moment.","Any version","Apply anyway","Auto-select restored.","Autoload cleared.","Autoload saved ({n}). These turn on when the game launches.","Autoload saved empty.","Cheat #{n}","Cheat file added.","Cheat file deleted.","Cheat file selected.","CheatRunner stopped responding during launch.","Cheats folder copied.","Checking {name}","Clear autoload ({n})","Comparing and downloading files one by one","Conflicts with another cheat","Copy failed. Select the text manually.","Could not clear autoload.","Could not copy diagnostics.","Could not disable cheats.","Could not load patches.","Could not load the game list.","Could not save autoload.","Could not save. Try again.","Could not select the file.","Could not start the download.","Could not start.","Could not toggle the cheat.","Delete failed.","Delete file","Delete {file}?","Diagnostics copied. Paste them into the issue.","Downloading {name}","Engine off","Everything is already up to date","Expand menu","Fetching the whole repository in one go","File v{a} / game v{b}","Finished {name}","Force apply {name}?","Game launched.","It targets another game version, so it may crash the game. Restore puts the original bytes back.","Launch failed.","Launch failed: {msg}","Launch request failed. The game may still be starting.","Launch timed out. Check if the game started.","Launching {name}...","Logs copied.","Made with {heart} by maj0r","Matches v{v}","Needs v{v}","No game","No games match your search.","No games with cheats yet. Get some from Sources.","No logs yet.","No matching lines.","Not ready","Nothing recent. Launch a game or open its trainer.","Nothing to copy.","Nothing was on.","Open trainer: {name}","Patch autoload cleared.","Patch autoload saved.","QR code is not available.","Reading the file list from GitHub","Remove from favorites","Reset failed.","Reset settings?","Saved","Saving the cheat files","Saving...","Send a new CheatRunner.elf from your PC afterwards.","Settings reset.","Shut down","Shut down CheatRunner?","Source {i} of {n}","Start the game first.","The game is still loading.","The game is still starting. Try again in a few seconds.","The game list is unavailable right now.","This cheat cannot be toggled right now.","This cheat crashed the game before. Clear the flag to retry.","This file is for a different game version than the one installed.","This removes the file from the console. You can download it again from Sources.","Took {t}","URL copied.","Unpacking {name}","Unsupported patch","Upload failed.","Use a .mc4, .shn or .json file.","Use another version?","Use anyway","Waiting for CheatRunner to come back...","{label} address copied","all sources","avg {s}","just now","the cheat file","v{v} matches","{n} cheat(s) turned off.","{n} new or updated file(s) saved","{n} title","{n} titles","{n}d ago","{n}h ago","{n}m ago","{page} - CheatRunner","{a} of {n} on","{h}h {m}m","{m}m","{s}s","{m}m {s}s","by {a}","Welcome to CheatRunner.","No cheat files yet. Grab community cheats from Sources, or open a game and drop a file in.","Cancel","Continue","Reconnecting...","Copied","Copied.","Unknown","Dialog","Off","Privileges","OK","Missing","Failed.","Launching","Try again","Game","Search cheats","Follow maj0r on X","Follow maj0r on Bluesky","Made by maj0r - ð§ð·","Leave the project a star! â­","Crimson","Autoload","Reset","No patches for this game.","Put your XML patches in /data/cheatrunner/patches/xml","Update available","Version {v} is available.","Browse cheats online","Search every game with cheats and check if yours is covered.","Open site","Community patch repositories","Ready-made patches for your games. Nothing is downloaded - the repository opens in a new tab.","Open PS-Game-Patch","Visual effects","Reduced turns off blur, shadows and animation. This is the default in the PS5 browser.","Full","Reduced","Show more ({n})","Recommended","Official PS-Game-Patch release. Patches are saved to /data/cheatrunner/patches.","Download patches","Saving the patch files","Some patches only work if applied at game start: save the autoload and restart the game.","Troubleshooting","Report a problem, or put every setting back to its default.","Copies the diagnostics and opens the issue page on GitHub.","Cheats, patches and autoload profiles are not touched.","Everything looks good.","Something needs attention - check the details below.","A .mc4, .shn or .json file from your computer.","Add one in Files or Sources","Appearance","Appearance, the cheat engine and controller options. Changes are saved as you make them.","Cheat file","Cheat files for this game","CheatRunner lists the games installed on this console. Install one, then refresh the list.","Cheats and patches cannot be applied until it is turned on.","Checking...","Choose another file","Could not load","Could not load the cheat file","Could not load the patches","Could not load the settings","Could not load your games","Could not read the live cheat state.","Download community cheats from Sources, or add a file from your computer.","Download one from Sources or drop a file below.","Download ready-made cheats and patches for your games. Downloads keep running while you browse.","Download the official patch set from Sources, or put your XML patches in /data/cheatrunner/patches/xml.","Downloading","Downloading {p}%","Downloads","Engine","Following the latest lines","Get cheats","Health","Library","Live health of the cheat engine and memory patching, and what CheatRunner has been doing.","News, help and the people behind CheatRunner.","No cheat file for this game yet","No cheats match \"{q}\".","No file yet","No games found yet","No local file found","No patches for this game","Now playing","Open Settings","Paused while you read","Preferences","Reduced removes every animation. This is the default in the PS5 browser.","Saved in /data/cheatrunner/cheats. The file marked In use is the one the trainer reads.","Send a report when something does not work.","Start it to use cheats","Start this game to use its cheats.","Technical details","The cheat engine is off.","The current values could not be read, so nothing is shown rather than showing wrong ones.","The essentials for applying cheats and patches.","The file is there, but it could not be read. It may be damaged or still being copied.","The file loaded but it is empty. Try another file for this game.","The game list is unavailable right now. Check that the console is awake, then try again.","The patch files for this game could not be read. Try again in a moment.","The switches may be out of date.","Theme, effects and language of this dashboard.","This cheat file has no cheats","Try again below","Turn it on in Settings","Danger zone","Jump to latest","Delete the file in use","Cheat source","Filter games","See your games","Credits","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"tr":{"ar-SA":["ØªØ³Ø¨Ø¨ ØºØ´ ÙÙ ØªØ¹Ø·Ù Ø§ÙÙØ¹Ø¨Ø© ÙÙ Ø§ÙÙØ±Ø© Ø§ÙØ³Ø§Ø¨ÙØ©.","Ø­ÙÙ","Ø¥Ø¬Ø±Ø§Ø¡","Ø¥Ø¬Ø±Ø§Ø¡Ø§Øª","Ø¥Ø¶Ø§ÙØ© ÙÙÙ","Ø¥Ø¶Ø§ÙØ© ØºØ´","Ø¥Ø¶Ø§ÙØ© Ø¥ÙÙ Ø§ÙÙÙØ¶ÙØ©","ÙØ¶ÙÙ Ø¥Ø¯Ø®Ø§Ù â CheatRunner Ø¥ÙÙ ÙØ§Ø¦ÙØ© Ø®ÙØ§Ø±Ø§Øª ÙÙ ÙØ¹Ø¨Ø©Ø ÙÙØ¬ÙÙØ¹Ø© Ø£Ø²Ø±Ø§Ø± ØªÙØªØ­ Ø§ÙÙØ¯Ø±Ø¨ Ø§ÙØ®Ø§Øµ Ø¨Ø§ÙÙØ¹Ø¨Ø© ÙÙØ¯ Ø§ÙØªØ´ØºÙÙ.","ÙØªÙØ¯Ù","Ø§ÙÙÙ","Ø³ØªÙØ®ØµÙÙØµ Ø¬ÙÙØ¹ Ø§ÙÙØ³Ø§ÙÙØ§Øª ÙÙØ¯ÙÙØ§ ÙÙ Ø´Ø±Ø§Ø¡ Ø¬ÙØ§Ø² PS5 Ø¨ÙØ¸Ø§Ù ØªØ´ØºÙÙ ÙÙØ®ÙØ¶ (1.xxâ4.xx) ÙÙØ¨Ø­Ø« ÙØ§ÙØªØ·ÙÙØ± ÙØ§ÙØªØ±ÙÙØ§Øª Ø§ÙÙØ³ØªÙØ¨ÙÙØ©.","ÙØ´ÙØª Ø¬ÙÙØ¹ Ø§ÙØªÙØ²ÙÙØ§Øª","ÙÙ Ø§ÙØ£ÙØ¹Ø§Ø¨","ØªØ­Ø¯ÙØ« Ø§ÙÙÙÙØ§Øª Ø§ÙØªÙ ØªØºÙÙØ±Øª Ø£ÙØ¶ÙØ§","ÙÙØ·Ø¨ÙÙÙ","ØªØ·Ø¨ÙÙ","Ø§ÙØªØ·Ø¨ÙÙØ§Øª","Ø§Ø®ØªÙØ§Ø± ØªÙÙØ§Ø¦Ù","Ø±Ø¬ÙØ¹","ØªØµÙÙØ­ ÙÙØªØ¨ØªÙ","Ø§ÙØ£Ø²Ø±Ø§Ø±","Buy Me a Coffee","ØºØ´","ÙØ­Ø±Ù Ø§ÙØºØ´","CheatRunner - ÙØ¯Ø±ÙØ¨ Ø§ÙØºØ´ ÙØ¬ÙØ§Ø² PS5 Ø§ÙØ®Ø§Øµ Ø¨Ù!","Ø§ÙØºØ´","ØªÙØ¤Ø¬ÙÙÙ Ø§ÙØºØ´ÙØ´ ÙÙØ°Ù Ø§ÙÙØ¯Ø© Ø¨Ø¹Ø¯ Ø¨Ø¯Ø¡ Ø§ÙÙØ¹Ø¨Ø©.","Ø§ÙØ£ÙØ¹Ø§Ø¨ Ø°Ø§Øª Ø§ÙØºØ´ Ø£ÙÙÙØ§","Ø§Ø®ØªØ± Ø´Ø¨ÙØ©","Circle","ÙØ³Ø­ Ø¹ÙØ§ÙÙ Ø§ÙØªØµÙÙØ©","ÙØ³Ø­ Ø§ÙØ¹ÙØ§ÙØ©","ÙØ³Ø­ Ø§ÙØ¨Ø­Ø«","Ø¥ØºÙØ§Ù","Ø·ÙÙ","Ø·ÙÙ Ø§ÙÙØ§Ø¦ÙØ©","ÙÙØ· Ø£ÙÙØ§Ù ÙÙØ­Ø© Ø§ÙØªØ­ÙÙ ÙØ°Ù.","Ø§ÙÙØ¬ØªÙØ¹","ÙØ³ØªÙØ¯Ø¹Ø§Øª Ø§ÙØºØ´ Ø§ÙÙØ¬ØªÙØ¹ÙØ©","Ø§Ø®ØªØµØ§Ø± ÙØ­Ø¯Ø© Ø§ÙØªØ­ÙÙ ÙÙØ§Ø¦ÙØ© Ø§ÙÙØ¹Ø¨Ø©","ÙØ³Ø®","ÙØ³Ø® Ø§ÙØ±Ø§Ø¨Ø·","ÙØ³Ø® Ø§ÙÙÙ","ÙØ³Ø® ÙØ¬ÙØ¯ Ø§ÙØºØ´","ÙØ³Ø® Ø§ÙØ±Ø§Ø¨Ø·","Ø§ÙØªØ´Ø§Ù Ø§ÙØ£Ø¹Ø·Ø§Ù","ØªØ³Ø¨Ø¨ ÙÙ ØªØ¹Ø·Ù Ø§ÙÙØ¹Ø¨Ø© Ø¢Ø®Ø± ÙØ±Ø©","Cross","Ø§ÙØ¹ÙÙØ§Øª Ø§ÙØ±ÙÙÙØ©","Ø§ÙØ¯Ø¹Ù Ø¨Ø§ÙØ¹ÙÙØ§Øª Ø§ÙØ±ÙÙÙØ©","Ø¯Ø§ÙÙ","Ø­Ø°Ù","ØªØ¹Ø·ÙÙ Ø§ÙÙÙ","Ø£Ø³ÙÙ","ØªÙØ²ÙÙ ÙÙ Ø§ÙÙØµØ§Ø¯Ø±","ÙØ²ÙÙ ØºØ´ÙØ´ Ø§ÙÙØ¬ØªÙØ¹","ÙØ´Ù Ø§ÙØªÙØ²ÙÙ","ÙØ²ÙÙ ÙÙÙØ§Øª ØºØ´ Ø¬Ø§ÙØ²Ø© ÙØ£ÙØ¹Ø§Ø¨Ù. ØªÙØ­ÙØ¸ ÙÙ /data/cheatrunner/cheats ÙØªØ³ØªÙØ± Ø§ÙØªÙØ²ÙÙØ§Øª ÙÙ Ø§ÙØ®ÙÙÙØ©.","ØªÙØ²ÙÙ Ø§ÙÙØ­Ø¯Ø¯","ØªÙ Ø§ÙØªÙØ²ÙÙ","Ø£ÙÙØª ÙÙÙ .mc4 Ø£Ù .shn Ø£Ù .json ÙÙØ§Ø Ø£Ù Ø§ÙÙØ± ÙÙØ§Ø³ØªØ¹Ø±Ø§Ø¶","ØªÙØ¹ÙÙ Ø§Ø®ØªØµØ§Ø± ÙØ­Ø¯Ø© Ø§ÙØªØ­ÙÙ","ØªÙØ¹ÙÙ Ø§ÙØ§Ø®ØªØµØ§Ø±","Ø§ÙÙØ­Ø±Ù ÙØ¹ÙÙ","Enter ÙÙÙØªØ­","Ø§ÙØ£Ø®Ø·Ø§Ø¡ ÙÙØ·","ÙØ·Ø§Ø¨ÙØ© ØªØ§ÙØ©","ÙØ´Ù","Ø§ÙÙÙØ¶ÙØ©","Ø§ÙÙÙÙØ§Øª","ØªØµÙÙØ©","ØªØµÙÙØ© Ø§ÙØ³Ø¬ÙØ§Øª","Ø¶Ø¹ Ø¹ÙØ§ÙØ© Ø¹ÙÙ Ø§ÙØºØ´ Ø¥Ø°Ø§ Ø£ÙØºÙÙØª Ø§ÙÙØ¹Ø¨Ø© ÙØ¨Ø§Ø´Ø±Ø© Ø¨Ø¹Ø¯ ØªÙØ¹ÙÙÙ.","ÙÙ Ø£Ø¬Ù Ø§ÙÙØ³ØªØ®Ø¯ÙÙÙ. Ø¨ÙØ§Ø³Ø·Ø© Ø§ÙÙØ³ØªØ®Ø¯ÙÙÙ.","ÙØ±Ø¶ Ø§ÙØªØ·Ø¨ÙÙ","Ø§ÙØ£ÙØ¹Ø§Ø¨","Ø£ÙØ¹Ø§Ø¨ Ø¨ÙØ§ ØºØ´","Ø£ÙØ¹Ø§Ø¨ Ø¨ÙØ§ ØªØµØ­ÙØ­Ø§Øª","Ø§ÙÙØ§ØµÙ Ø¨ÙÙ Ø§ÙØºØ´ÙØ´ (ÙÙÙÙ Ø«Ø§ÙÙØ©)","Ø¹Ø§Ù","ØªÙØ¶ØºØ· ÙØ¹ÙØ§ ÙØªÙØ¹ÙÙ Ø§ÙØ§Ø®ØªØµØ§Ø±.","ÙØ¯Ø© Ø§ÙØ¶ØºØ· (ÙÙÙÙ Ø«Ø§ÙÙØ©)","Ø§Ø¶ØºØ· Ø²Ø±ÙÙ ÙØ¹ÙØ§ ÙÙØªØ­ Ø§ÙÙØ¯Ø±Ø¨ Ø§ÙØ®Ø§Øµ Ø¨Ø§ÙÙØ¹Ø¨Ø© ÙÙØ¯ Ø§ÙØªØ´ØºÙÙ.","Ø¨ÙØ§Ø·Ø© Ø§ÙØ´Ø§Ø´Ø© Ø§ÙØ±Ø¦ÙØ³ÙØ©","ÙØªØµÙ Ø¨ÙØ§Ø¬ÙØ© Ø§ÙÙØ¸Ø§Ù. ÙØ¹Ø·ÙÙ Ø§ÙØªØ±Ø§Ø¶ÙÙØ§ - ÙØ¯ ÙØªØ³Ø¨Ø¨ ÙÙ ØªØ¹Ø·Ù Ø§ÙØ´Ø§Ø´Ø© Ø§ÙØ±Ø¦ÙØ³ÙØ© ÙÙ Ø¨Ø¹Ø¶ Ø§ÙØ¥ØµØ¯Ø§Ø±Ø§Øª.","Ø§ÙÙØ¯Ø© Ø§ÙÙØ§Ø²ÙØ© ÙØ¥Ø¨ÙØ§Ø¡ Ø§ÙÙØ¬ÙÙØ¹Ø© ÙØ¶ØºÙØ·Ø© ÙØ¨Ù Ø§ÙØªÙØ¹ÙÙ.","ÙÙØ¯Ø§Ø± Ø§ÙØªÙØ§ØµÙÙ Ø§ÙÙÙØªÙØ¨Ø© ÙÙ Ø§ÙØ³Ø¬Ù.","Ø¥Ø°Ø§ ÙØ§Ù CheatRunner ÙÙÙØ¯ÙØ§ ÙÙØ ÙÙÙØ± ÙÙ Ø¯Ø¹Ù ØªØ·ÙÙØ± Ø§ÙÙØ´Ø±ÙØ¹! ð«¶","ÙÙØ¯ Ø§ÙØ§Ø³ØªØ®Ø¯Ø§Ù","ÙØ«Ø¨ÙØª Ø¨ÙØ§Ø·Ø© CheatRunner Ø¹ÙÙ Ø§ÙØ´Ø§Ø´Ø© Ø§ÙØ±Ø¦ÙØ³ÙØ© ÙÙ PS5 Ø¹ÙØ¯ Ø§ÙØªØ´ØºÙÙ.","Ø§ÙØ¶Ù Ø¥ÙÙ Ø¯ÙØ³ÙÙØ±Ø¯","Ko-FiØ Ø§ÙÙÙÙØ©Ø Ø§ÙØ¹ÙÙØ§Øª Ø§ÙØ±ÙÙÙØ©Ø PIX","Ø§ÙÙØºØ©","ÙØºØ© ÙÙØ­Ø© Ø§ÙØªØ­ÙÙ ÙØ°Ù.","Ø¢Ø®Ø± ÙØ¹Ø¨","ÙØ³Ø§Ø±","ÙØ§ØªØ­","Ø­Ø§ÙØ© ÙØ­Ø±Ù Ø§ÙØºØ´ ÙØªØ¹Ø¯ÙÙ Ø§ÙØ°Ø§ÙØ±Ø©.","ÙÙÙØ§Øª Ø§ÙØºØ´ Ø§ÙÙØ­ÙÙØ©","ÙØ³ØªÙÙ Ø§ÙØ³Ø¬Ù","Ø¹Ø§ÙÙ ØªØµÙÙØ© ÙØ³ØªÙÙ Ø§ÙØ³Ø¬Ù","Ø§ÙØ³Ø¬ÙØ§Øª","Ø§ÙØ±Ø¦ÙØ³ÙØ©","Ø§ÙÙÙØªØ§Ø­ Ø§ÙØ±Ø¦ÙØ³Ù ÙØªØ·Ø¨ÙÙ Ø§ÙØºØ´ÙØ´ ÙØ§ÙØªØµØ­ÙØ­Ø§Øª.","Ø¥ØµÙØ§Ø­ Ø§ÙÙÙØ¯ Ø§ÙØ±Ø¦ÙØ³Ù","ÙØ¯ ÙØ§ ÙØ·Ø§Ø¨Ù Ø¥ØµØ¯Ø§Ø± Ø§ÙÙØ¹Ø¨Ø© ÙØ°Ø§","ØªØ¹Ø¯ÙÙ Ø§ÙØ°Ø§ÙØ±Ø©","ÙÙØªØµÙ Ø§ÙÙÙÙ","Ø§ÙØ­Ø¯ Ø§ÙØ£Ø¯ÙÙ ÙÙÙÙØª Ø¨ÙÙ ØªØ¨Ø¯ÙÙÙÙ ÙÙØºØ´.","Ø§ÙØ§Ø³Ù Ø£-Ù","Ø§ÙØ§Ø³Ù Ù-Ø£","ÙØ§ ÙÙØ¬Ø¯ ÙÙÙ ØºØ´ ÙÙØ°Ù Ø§ÙÙØ¹Ø¨Ø© Ø¨Ø¹Ø¯.","ÙØ§ ØªÙØ¬Ø¯ ÙÙØ¶ÙØ§Øª Ø¨Ø¹Ø¯. Ø§Ø¶ØºØ· Ø¹ÙÙ ÙØ¬ÙØ© ÙØ¹Ø¨Ø© ÙØ¥Ø¶Ø§ÙØªÙØ§.","ØºÙØ± ÙÙØ¯ Ø§ÙØªØ´ØºÙÙ","ÙØ¹ÙÙ","Ø§ÙØªØ­ Ø¹ÙÙ ÙØ§ØªÙÙ","Ø§ÙØªØ­ Ø¹ÙÙ ÙØ§ØªÙÙ (Ø±ÙØ² QR)","Ø§ÙØªØ­ ÙÙØ­Ø© Ø§ÙØªØ­ÙÙ Ø¹ÙÙ ÙØ§ØªÙÙ","ÙØªØ­ Ø§ÙÙØ¯Ø±Ø¨","Ø§ÙØ®ÙØ§Ø±Ø§Øª","Ø¥ØµØ¯Ø§Ø± Ø¢Ø®Ø±","ØµÙØ­Ø©","Ø§ÙØªØµØ­ÙØ­Ø§Øª","ØªØ´ØºÙÙ","Ø±ÙØ² QR","Ø±ÙØ² QR ÙØ¹ÙÙØ§Ù ÙÙØ­Ø© Ø§ÙØªØ­ÙÙ","Ø¥Ø¹Ø§Ø¯Ø© ØªØ·Ø¨ÙÙ Ø§ÙØºØ´ÙØ´ Ø§ÙÙÙØ¹ÙÙØ© ÙÙ ÙÙ ÙØ±Ø© ØªÙØ´ØºÙÙÙ ÙÙÙØ§ ÙØ°Ù Ø§ÙÙØ¹Ø¨Ø©","Ø¬Ø§ÙØ²","Ø§ÙØ£Ø®ÙØ±Ø©","ØªØ­Ø¯ÙØ«","ØªØ­Ø¯ÙØ« ÙØ§Ø¦ÙØ© Ø§ÙØ£ÙØ¹Ø§Ø¨","Ø¥Ø¹Ø§Ø¯Ø© Ø§ÙØªØ´ØºÙÙ","Ø§ÙØ¥Ø¨ÙØ§Øº Ø¹Ù ÙØ´ÙÙØ©","Ø¥Ø¹Ø§Ø¯Ø© ÙØ­Øµ Ø§ÙØ£ÙØ¹Ø§Ø¨ Ø§ÙÙØ«Ø¨ØªØ©","Ø¥Ø¹Ø§Ø¯Ø© Ø§ÙØ¶Ø¨Ø· Ø§ÙØ§ÙØªØ±Ø§Ø¶Ù","Ø­ÙÙ Ø§ÙØºØ´ÙØ´ Ø§ÙÙØ¹ØªÙØ¯Ø© Ø¹ÙÙ Ø§ÙÙÙØ¯ Ø§ÙØ±Ø¦ÙØ³Ù ØªÙÙØ§Ø¦ÙÙØ§.","Ø§Ø³ØªØ¹Ø§Ø¯Ø©","ÙÙÙÙ","ÙÙØ¯ Ø§ÙØªØ´ØºÙÙ","ÙÙØ¯ Ø§ÙØªØ´ØºÙÙ Ø§ÙØ¢Ù","Ø­ÙØ¸ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù","Ø§ÙØ³Ø­Ù Ø¨ÙØ§ÙÙØ±Ø§ ÙØ§ØªÙÙ. ÙØ¬Ø¨ Ø£Ù ÙÙÙÙ ÙØ§ØªÙÙ Ø¹ÙÙ ÙÙØ³ Ø´Ø¨ÙØ© Wi-Fi Ø§ÙØ®Ø§ØµØ© Ø¨Ù PS5.","Ø¨Ø­Ø«","Ø§Ø¨Ø­Ø« Ø¨Ø§ÙØ§Ø³Ù Ø£Ù ÙØ¹Ø±ÙÙ Ø§ÙØ¹ÙÙØ§Ù","Ø§Ø¨Ø­Ø« Ø¹Ù Ø§ÙØ£ÙØ¹Ø§Ø¨","Ø§Ø¨Ø­Ø« Ø¹Ù Ø§ÙØ£ÙØ¹Ø§Ø¨ ÙØ§ÙØµÙØ­Ø§Øª ÙØ§ÙØ¥Ø¬Ø±Ø§Ø¡Ø§Øª","Ø§Ø®ØªØ± Ø¹ÙÙØ§ÙÙØ§ ÙÙØ³Ø®Ù ÙØ§ÙÙÙØ§.","Ø§ÙØ¥Ø¹Ø¯Ø§Ø¯Ø§Øª","Ø¹Ø±Ø¶ Ø±ÙØ² QR","Ø¥ÙÙØ§Ù Ø§ÙÙ payload","ØªØ±ØªÙØ¨ Ø§ÙØ£ÙØ¹Ø§Ø¨","Ø§ÙÙØµØ¯Ø±","Ø§ÙÙØµØ§Ø¯Ø±","Ø§ÙØ³Ø±Ø¹Ø©","Square","Ø§Ø¨Ø¯Ø£ Ø§ÙÙØ¹Ø¨Ø©","Ø§Ø¨Ø¯Ø£ ÙØ°Ù Ø§ÙÙØ¹Ø¨Ø© ÙØªØ·Ø¨ÙÙ Ø§ÙØªØµØ­ÙØ­Ø§Øª.","Ø§ÙØ­Ø§ÙØ©","Ø§ÙØ­Ø§ÙØ© ÙØ§ÙØ³Ø¬ÙØ§Øª","Ø§Ø¯Ø¹Ù CheatRunner","Ø¯Ø¹Ù ÙÙ Ø§ÙØ¨Ø±Ø§Ø²ÙÙ","Ø§Ø¯Ø¹Ù ÙØ°Ø§ Ø§ÙÙØ´Ø±ÙØ¹","Ø§ÙØ¯Ø§Ø¹ÙÙÙ","Ø§ÙÙØ¸Ø§Ù","Ø§ÙØ£Ø³Ø§Ø³ÙØ§Øª - Ø§ÙØ³ÙØ© ÙÙÙØªØ§Ø­ ÙØ­Ø±Ù Ø§ÙØºØ´.","Ø§ÙØ³ÙØ©","Ø§ÙØ³ÙØ© ÙÙØ­Ø±Ù Ø§ÙØºØ´","Ø§ÙÙÙØª Ø§ÙÙØªØ¨ÙÙ","ÙØ¯Ø© Ø§ÙÙØ¹Ø¨","ÙØ¹Ø±ÙÙ Ø§ÙØ¹ÙÙØ§Ù","ÙÙØ§ØªÙØ­ Ø§ÙØªØ¨Ø¯ÙÙ","TouchPad","Ø§ÙÙØ¯Ø±Ø¨","Triangle","Ø£Ø¹ÙÙ","ÙØ­Ø¯ÙØ«Ø©","Ø§Ø³ØªØ®Ø¯Ø§Ù","Ø§ÙØ¥ØµØ¯Ø§Ø±","Ø§ÙØ§ÙØªØ¸Ø§Ø± Ø¨Ø¹Ø¯ Ø¨Ø¯Ø¡ Ø§ÙÙØ¹Ø¨Ø© (ÙÙÙÙ Ø«Ø§ÙÙØ©)","Ø§ÙØªØ­Ø°ÙØ±Ø§Øª ÙØ§ÙØ£Ø®Ø·Ø§Ø¡","ÙØ§ ÙØ§Ù ÙÙØ¹ÙÙ CheatRunner - ÙÙÙØ¯ Ø¹ÙØ¯ÙØ§ ÙØ§ ÙØ¹ÙÙ Ø´ÙØ¡.","ÙØ¹ ØºØ´","ââ ØªÙÙÙÙ","Ø³ØªØ¹ÙØ¯ ÙÙ Ø§ÙØ¥Ø¹Ø¯Ø§Ø¯Ø§Øª Ø¥ÙÙ Ø§ÙØ§ÙØªØ±Ø§Ø¶Ù. ÙØ¨ÙÙ ÙÙÙØ° Ø§ÙÙÙØ¨ ÙÙØ§ ÙÙ.","ÙØ¬Ø±Ù ØªØ·Ø¨ÙÙ ØºØ´ Ø¢Ø®Ø±. Ø­Ø§ÙÙ ÙØ±Ø© Ø£Ø®Ø±Ù Ø¨Ø¹Ø¯ ÙÙÙÙ.","Ø£Ù Ø¥ØµØ¯Ø§Ø±","ØªØ·Ø¨ÙÙ Ø¹ÙÙ Ø£Ù Ø­Ø§Ù","ØªÙØª Ø§Ø³ØªØ¹Ø§Ø¯Ø© Ø§ÙØ§Ø®ØªÙØ§Ø± Ø§ÙØªÙÙØ§Ø¦Ù.","ØªÙ ÙØ³Ø­ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù.","ØªÙ Ø­ÙØ¸ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù ({n}). Ø³ØªØ¹ÙÙ Ø¹ÙØ¯ Ø¨Ø¯Ø¡ Ø§ÙÙØ¹Ø¨Ø©.","ØªÙ Ø­ÙØ¸ ØªØ­ÙÙÙ ØªÙÙØ§Ø¦Ù ÙØ§Ø±Øº.","ØºØ´ Ø±ÙÙ {n}","ØªÙØª Ø¥Ø¶Ø§ÙØ© ÙÙÙ Ø§ÙØºØ´.","ØªÙ Ø­Ø°Ù ÙÙÙ Ø§ÙØºØ´.","ØªÙ Ø§Ø®ØªÙØ§Ø± ÙÙÙ Ø§ÙØºØ´.","ØªÙÙÙ CheatRunner Ø¹Ù Ø§ÙØ§Ø³ØªØ¬Ø§Ø¨Ø© Ø£Ø«ÙØ§Ø¡ Ø§ÙØªØ´ØºÙÙ.","ØªÙ ÙØ³Ø® ÙØ¬ÙØ¯ Ø§ÙØºØ´.","Ø¬Ø§Ø±Ù ÙØ­Øµ {name}","ÙØ³Ø­ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù ({n})","ÙÙØ§Ø±ÙØ© Ø§ÙÙÙÙØ§Øª ÙØªÙØ²ÙÙÙØ§ ÙØ§Ø­Ø¯ÙØ§ ØªÙÙ Ø§ÙØ¢Ø®Ø±","ÙØªØ¹Ø§Ø±Ø¶ ÙØ¹ ØºØ´ Ø¢Ø®Ø±","ÙØ´Ù Ø§ÙÙØ³Ø®. Ø­Ø¯ÙØ¯ Ø§ÙÙØµ ÙØ¯ÙÙÙØ§.","ØªØ¹Ø°ÙØ± ÙØ³Ø­ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù.","ØªØ¹Ø°ÙØ± ÙØ³Ø® Ø§ÙØªØ´Ø®ÙØµ.","ØªØ¹Ø°ÙØ± ØªØ¹Ø·ÙÙ Ø§ÙØºØ´ÙØ´.","ØªØ¹Ø°ÙØ± ØªØ­ÙÙÙ Ø§ÙØªØµØ­ÙØ­Ø§Øª.","ØªØ¹Ø°ÙØ± ØªØ­ÙÙÙ ÙØ§Ø¦ÙØ© Ø§ÙØ£ÙØ¹Ø§Ø¨.","ØªØ¹Ø°ÙØ± Ø­ÙØ¸ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù.","ØªØ¹Ø°ÙØ± Ø§ÙØ­ÙØ¸. Ø­Ø§ÙÙ ÙØ±Ø© Ø£Ø®Ø±Ù.","ØªØ¹Ø°ÙØ± Ø§Ø®ØªÙØ§Ø± Ø§ÙÙÙÙ.","ØªØ¹Ø°ÙØ± Ø¨Ø¯Ø¡ Ø§ÙØªÙØ²ÙÙ.","ØªØ¹Ø°ÙØ± Ø§ÙØ¨Ø¯Ø¡.","ØªØ¹Ø°ÙØ± ØªØ¨Ø¯ÙÙ Ø§ÙØºØ´.","ÙØ´Ù Ø§ÙØ­Ø°Ù.","Ø­Ø°Ù Ø§ÙÙÙÙ","Ø­Ø°Ù {file}Ø","ØªÙ ÙØ³Ø® Ø§ÙØªØ´Ø®ÙØµ. Ø§ÙØµÙÙ ÙÙ Ø§ÙØ¨ÙØ§Øº.","Ø¬Ø§Ø±Ù ØªÙØ²ÙÙ {name}","Ø§ÙÙØ­Ø±Ù ÙØªÙÙÙ","ÙÙ Ø´ÙØ¡ ÙØ­Ø¯ÙØ« Ø¨Ø§ÙÙØ¹Ù","ØªÙØ³ÙØ¹ Ø§ÙÙØ§Ø¦ÙØ©","Ø¬ÙØ¨ Ø§ÙÙØ³ØªÙØ¯Ø¹ Ø¨Ø§ÙÙØ§ÙÙ Ø¯ÙØ¹Ø© ÙØ§Ø­Ø¯Ø©","Ø§ÙÙÙÙ v{a} / Ø§ÙÙØ¹Ø¨Ø© v{b}","Ø§ÙØªÙÙ {name}","ÙØ±Ø¶ ØªØ·Ø¨ÙÙ {name}Ø","ØªÙ ØªØ´ØºÙÙ Ø§ÙÙØ¹Ø¨Ø©.","ÙØ³ØªÙØ¯Ù Ø¥ØµØ¯Ø§Ø±ÙØ§ Ø¢Ø®Ø± ÙÙ Ø§ÙÙØ¹Ø¨Ø© ÙÙØ¯ ÙØªØ³Ø¨Ø¨ ÙÙ ØªØ¹Ø·ÙÙØ§. ØªØ¹ÙØ¯ Ø§ÙØ§Ø³ØªØ¹Ø§Ø¯Ø© Ø§ÙØ¨Ø§ÙØªØ§Øª Ø§ÙØ£ØµÙÙØ©.","ÙØ´Ù Ø§ÙØªØ´ØºÙÙ.","ÙØ´Ù Ø§ÙØªØ´ØºÙÙ: {msg}","ÙØ´Ù Ø·ÙØ¨ Ø§ÙØªØ´ØºÙÙ. ÙØ¯ ØªÙÙÙ Ø§ÙÙØ¹Ø¨Ø© ÙØ§ ØªØ²Ø§Ù ÙÙØ¯ Ø§ÙØ¨Ø¯Ø¡.","Ø§ÙØªÙØª ÙÙÙØ© Ø§ÙØªØ´ØºÙÙ. ØªØ­ÙÙ ÙÙØ§ Ø¥Ø°Ø§ Ø¨Ø¯Ø£Øª Ø§ÙÙØ¹Ø¨Ø©.","Ø¬Ø§Ø±Ù ØªØ´ØºÙÙ {name}...","ØªÙ ÙØ³Ø® Ø§ÙØ³Ø¬ÙØ§Øª.","ØµÙÙØ¹ Ø¨Ù {heart} Ø¨ÙØ§Ø³Ø·Ø© maj0r","ÙØ·Ø§Ø¨Ù v{v}","ÙØªØ·ÙØ¨ v{v}","ÙØ§ ØªÙØ¬Ø¯ ÙØ¹Ø¨Ø©","ÙØ§ ØªÙØ¬Ø¯ Ø£ÙØ¹Ø§Ø¨ ØªØ·Ø§Ø¨Ù Ø¨Ø­Ø«Ù.","ÙØ§ ØªÙØ¬Ø¯ Ø£ÙØ¹Ø§Ø¨ Ø¨ÙØ§ ØºØ´ Ø¨Ø¹Ø¯. Ø§Ø­ØµÙ Ø¹ÙÙÙØ§ ÙÙ Ø§ÙÙØµØ§Ø¯Ø±.","ÙØ§ ØªÙØ¬Ø¯ Ø³Ø¬ÙØ§Øª Ø¨Ø¹Ø¯.","ÙØ§ ØªÙØ¬Ø¯ Ø£Ø³Ø·Ø± ÙØ·Ø§Ø¨ÙØ©.","ØºÙØ± Ø¬Ø§ÙØ²","ÙØ§ Ø´ÙØ¡ Ø­Ø¯ÙØ«. Ø´ØºÙÙ ÙØ¹Ø¨Ø© Ø£Ù Ø§ÙØªØ­ Ø§ÙÙØ¯Ø±Ø¨ Ø§ÙØ®Ø§Øµ Ø¨ÙØ§.","ÙØ§ Ø´ÙØ¡ ÙÙØ³Ø®Ù.","ÙÙ ÙÙÙ Ø´ÙØ¡ ÙÙØ¹ÙÙÙØ§.","ÙØªØ­ Ø§ÙÙØ¯Ø±Ø¨: {name}","ØªÙ ÙØ³Ø­ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù ÙÙØªØµØ­ÙØ­Ø§Øª.","ØªÙ Ø­ÙØ¸ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù ÙÙØªØµØ­ÙØ­Ø§Øª.","Ø±ÙØ² QR ØºÙØ± ÙØªØ§Ø­.","ÙØ±Ø§Ø¡Ø© ÙØ§Ø¦ÙØ© Ø§ÙÙÙÙØ§Øª ÙÙ GitHub","Ø¥Ø²Ø§ÙØ© ÙÙ Ø§ÙÙÙØ¶ÙØ©","ÙØ´ÙØª Ø¥Ø¹Ø§Ø¯Ø© Ø§ÙØ¶Ø¨Ø·.","Ø¥Ø¹Ø§Ø¯Ø© Ø¶Ø¨Ø· Ø§ÙØ¥Ø¹Ø¯Ø§Ø¯Ø§ØªØ","ØªÙ Ø§ÙØ­ÙØ¸","Ø­ÙØ¸ ÙÙÙØ§Øª Ø§ÙØºØ´","Ø¬Ø§Ø±Ù Ø§ÙØ­ÙØ¸...","Ø¨Ø¹Ø¯ Ø°ÙÙ Ø£Ø±Ø³Ù ÙÙÙ CheatRunner.elf Ø¬Ø¯ÙØ¯ÙØ§ ÙÙ Ø­Ø§Ø³ÙØ¨Ù.","ØªÙØª Ø¥Ø¹Ø§Ø¯Ø© Ø¶Ø¨Ø· Ø§ÙØ¥Ø¹Ø¯Ø§Ø¯Ø§Øª.","Ø¥ÙÙØ§Ù","Ø¥ÙÙØ§Ù CheatRunnerØ","Ø§ÙÙØµØ¯Ø± {i} ÙÙ {n}","Ø´ØºÙÙ Ø§ÙÙØ¹Ø¨Ø© Ø£ÙÙÙØ§.","Ø§ÙÙØ¹Ø¨Ø© ÙØ§ ØªØ²Ø§Ù ÙÙØ¯ Ø§ÙØªØ­ÙÙÙ.","Ø§ÙÙØ¹Ø¨Ø© ÙØ§ ØªØ²Ø§Ù ÙÙØ¯ Ø§ÙØ¨Ø¯Ø¡. Ø­Ø§ÙÙ ÙØ±Ø© Ø£Ø®Ø±Ù Ø¨Ø¹Ø¯ Ø¨Ø¶Ø¹ Ø«ÙØ§ÙÙ.","ÙØ§Ø¦ÙØ© Ø§ÙØ£ÙØ¹Ø§Ø¨ ØºÙØ± ÙØªØ§Ø­Ø© Ø­Ø§ÙÙÙØ§.","ÙØ§ ÙÙÙÙ ØªØ¨Ø¯ÙÙ ÙØ°Ø§ Ø§ÙØºØ´ Ø§ÙØ¢Ù.","ØªØ³Ø¨Ø¨ ÙØ°Ø§ Ø§ÙØºØ´ ÙÙ ØªØ¹Ø·Ù Ø§ÙÙØ¹Ø¨Ø© ÙÙ ÙØ¨Ù. Ø§ÙØ³Ø­ Ø§ÙØ¹ÙØ§ÙØ© ÙØ¥Ø¹Ø§Ø¯Ø© Ø§ÙÙØ­Ø§ÙÙØ©.","ÙØ°Ø§ Ø§ÙÙÙÙ ÙØ®ØµØµ ÙØ¥ØµØ¯Ø§Ø± ÙØ®ØªÙÙ Ø¹Ù Ø§ÙÙØ«Ø¨ÙØª ÙÙ Ø§ÙÙØ¹Ø¨Ø©.","Ø³ÙØ¤Ø¯Ù ÙØ°Ø§ Ø¥ÙÙ Ø­Ø°Ù Ø§ÙÙÙÙ ÙÙ Ø§ÙØ¬ÙØ§Ø². ÙÙÙÙÙ ØªÙØ²ÙÙÙ ÙØ¬Ø¯Ø¯ÙØ§ ÙÙ Ø§ÙÙØµØ§Ø¯Ø±.","Ø§Ø³ØªØºØ±Ù {t}","ØªÙ ÙØ³Ø® Ø§ÙØ±Ø§Ø¨Ø·.","Ø¬Ø§Ø±Ù ÙÙ Ø¶ØºØ· {name}","ØªØµØ­ÙØ­ ØºÙØ± ÙØ¯Ø¹ÙÙ","ÙØ´Ù Ø§ÙØ±ÙØ¹.","Ø§Ø³ØªØ®Ø¯Ù ÙÙÙ .mc4 Ø£Ù .shn Ø£Ù .json.","Ø§Ø³ØªØ®Ø¯Ø§Ù Ø¥ØµØ¯Ø§Ø± Ø¢Ø®Ø±Ø","Ø§Ø³ØªØ®Ø¯Ø§Ù Ø¹ÙÙ Ø£Ù Ø­Ø§Ù","Ø¨Ø§ÙØªØ¸Ø§Ø± Ø¹ÙØ¯Ø© CheatRunner...","ØªÙ ÙØ³Ø® Ø¹ÙÙØ§Ù {label}","ÙÙ Ø§ÙÙØµØ§Ø¯Ø±","Ø§ÙÙØ¹Ø¯Ù {s}","Ø§ÙØ¢Ù","ÙÙÙ Ø§ÙØºØ´","ÙØ·Ø§Ø¨Ù v{v}","ØªÙ Ø¥ÙÙØ§Ù {n} ØºØ´.","ØªÙ Ø­ÙØ¸ {n} ÙÙÙ Ø¬Ø¯ÙØ¯ Ø£Ù ÙØ­Ø¯ÙØ«","{n} Ø¹ÙÙØ§Ù","{n} Ø¹ÙØ§ÙÙÙ","ÙÙØ° {n} ÙÙÙ","ÙÙØ° {n} Ø³Ø§Ø¹Ø©","ÙÙØ° {n} Ø¯ÙÙÙØ©","{page} - CheatRunner","{a} ÙÙ {n} ÙÙØ¹ÙÙ","{h} Ø³ {m} Ø¯","{m} Ø¯","{s} Ø«","{m} Ø¯ {s} Ø«","Ø¨ÙØ§Ø³Ø·Ø© {a}","ÙØ±Ø­Ø¨ÙØ§ Ø¨Ù ÙÙ CheatRunner.","ÙØ§ ØªÙØ¬Ø¯ ÙÙÙØ§Øª ØºØ´ Ø¨Ø¹Ø¯. Ø§Ø­ØµÙ Ø¹ÙÙ ØºØ´ÙØ´ Ø§ÙÙØ¬ØªÙØ¹ ÙÙ Ø§ÙÙØµØ§Ø¯Ø±Ø Ø£Ù Ø§ÙØªØ­ ÙØ¹Ø¨Ø© ÙØ£ÙÙØª ÙÙÙÙØ§.","Ø¥ÙØºØ§Ø¡","ÙØªØ§Ø¨Ø¹Ø©","Ø¬Ø§Ø±Ù Ø¥Ø¹Ø§Ø¯Ø© Ø§ÙØ§ØªØµØ§Ù...","ØªÙ Ø§ÙÙØ³Ø®","ØªÙ Ø§ÙÙØ³Ø®.","ØºÙØ± ÙØ¹Ø±ÙÙ","ÙØ±Ø¨Ø¹ Ø­ÙØ§Ø±","ÙØªÙÙÙ","Ø§ÙØµÙØ§Ø­ÙØ§Øª","Ø³ÙÙÙ","ÙÙÙÙØ¯","ÙØ´Ù.","Ø¬Ø§Ø±Ù Ø§ÙØªØ´ØºÙÙ","Ø¥Ø¹Ø§Ø¯Ø© Ø§ÙÙØ­Ø§ÙÙØ©","ÙØ¹Ø¨Ø©","Ø§Ø¨Ø­Ø« Ø¹Ù Ø§ÙØºØ´ÙØ´","ØªØ§Ø¨Ø¹ maj0r Ø¹ÙÙ X","ØªØ§Ø¨Ø¹ maj0r Ø¹ÙÙ Bluesky","ÙÙ ØµÙØ¹ maj0r - ð§ð·","Ø§ØªØ±Ù ÙØ¬ÙØ© ÙÙÙØ´Ø±ÙØ¹! â­","ÙØ±ÙØ²Ù","ØªØ­ÙÙÙ ØªÙÙØ§Ø¦Ù","Ø¥Ø¹Ø§Ø¯Ø© Ø¶Ø¨Ø·","ÙØ§ ØªÙØ¬Ø¯ ØªØµØ­ÙØ­Ø§Øª ÙÙØ°Ù Ø§ÙÙØ¹Ø¨Ø©.","Ø¶Ø¹ ØªØµØ­ÙØ­Ø§Øª XML Ø§ÙØ®Ø§ØµØ© Ø¨Ù ÙÙ /data/cheatrunner/patches/xml","ØªØ­Ø¯ÙØ« ÙØªØ§Ø­","Ø§ÙØ¥ØµØ¯Ø§Ø± {v} ÙØªØ§Ø­.","ØªØµÙØ­ Ø§ÙØºØ´ Ø¹Ø¨Ø± Ø§ÙØ¥ÙØªØ±ÙØª","Ø§Ø¨Ø­Ø« ÙÙ ÙÙ Ø§ÙØ£ÙØ¹Ø§Ø¨ Ø§ÙØªÙ ÙÙØ§ ØºØ´ ÙØªØ­ÙÙ ÙÙØ§ Ø¥Ø°Ø§ ÙØ§ÙØª ÙØ¹Ø¨ØªÙ ÙØ´ÙÙÙØ©.","ÙØªØ­ Ø§ÙÙÙÙØ¹","ÙØ³ØªÙØ¯Ø¹Ø§Øª ØªØµØ­ÙØ­Ø§Øª Ø§ÙÙØ¬ØªÙØ¹","ØªØµØ­ÙØ­Ø§Øª Ø¬Ø§ÙØ²Ø© ÙØ£ÙØ¹Ø§Ø¨Ù. ÙØ§ ÙØªÙ ØªÙØ²ÙÙ Ø£Ù Ø´ÙØ¡Ø Ø¨Ù ÙÙÙØªØ­ Ø§ÙÙØ³ØªÙØ¯Ø¹ ÙÙ ØªØ¨ÙÙØ¨ Ø¬Ø¯ÙØ¯.","ÙØªØ­ PS-Game-Patch","Ø§ÙÙØ¤Ø«Ø±Ø§Øª Ø§ÙÙØ±Ø¦ÙØ©","Ø§ÙÙØ¶Ø¹ Ø§ÙÙØ®ÙÙÙ ÙÙÙÙ Ø§ÙØªÙÙÙÙ ÙØ§ÙØ¸ÙØ§Ù ÙØ§ÙØ­Ø±ÙØ§Øª. ÙÙÙ Ø§ÙØ§ÙØªØ±Ø§Ø¶Ù ÙÙ ÙØªØµÙØ­ PS5.","ÙØ§ÙÙØ©","ÙØ®ÙÙÙØ©","Ø¹Ø±Ø¶ Ø§ÙÙØ²ÙØ¯ ({n})","ÙÙØµÙ Ø¨Ù","Ø¥ØµØ¯Ø§Ø± PS-Game-Patch Ø§ÙØ±Ø³ÙÙ. ØªÙØ­ÙØ¸ Ø§ÙØªØµØ­ÙØ­Ø§Øª ÙÙ /data/cheatrunner/patches.","ØªÙØ²ÙÙ Ø§ÙØªØµØ­ÙØ­Ø§Øª","Ø­ÙØ¸ ÙÙÙØ§Øª Ø§ÙØªØµØ­ÙØ­Ø§Øª","Ø¨Ø¹Ø¶ Ø§ÙØªØµØ­ÙØ­Ø§Øª ÙØ§ ØªØ¹ÙÙ Ø¥ÙØ§ Ø¥Ø°Ø§ Ø·ÙØ¨ÙÙØª Ø¹ÙØ¯ Ø¨Ø¯Ø¡ Ø§ÙÙØ¹Ø¨Ø©: Ø§Ø­ÙØ¸ Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù Ø«Ù Ø£Ø¹Ø¯ ØªØ´ØºÙÙ Ø§ÙÙØ¹Ø¨Ø©.","Ø§Ø³ØªÙØ´Ø§Ù Ø§ÙØ£Ø®Ø·Ø§Ø¡","Ø£Ø¨ÙØº Ø¹Ù ÙØ´ÙÙØ©Ø Ø£Ù Ø£Ø¹Ø¯ ÙÙ Ø§ÙØ¥Ø¹Ø¯Ø§Ø¯Ø§Øª Ø¥ÙÙ ÙÙÙÙØ§ Ø§ÙØ§ÙØªØ±Ø§Ø¶ÙØ©.","ÙÙØ³Ø® Ø§ÙØªØ´Ø®ÙØµØ§Øª ÙÙÙØªØ­ ØµÙØ­Ø© Ø§ÙÙØ´ÙÙØ§Øª Ø¹ÙÙ GitHub.","ÙØ§ ÙØªÙ Ø§ÙÙØ³Ø§Ø³ Ø¨Ø§ÙØºØ´ ÙØ§ÙØªØµØ­ÙØ­Ø§Øª ÙÙÙÙØ§Øª Ø§ÙØªØ­ÙÙÙ Ø§ÙØªÙÙØ§Ø¦Ù.","ÙÙ Ø´ÙØ¡ ÙØ¨Ø¯Ù Ø¬ÙØ¯ÙØ§.","ÙÙØ§Ù ÙØ§ ÙØ­ØªØ§Ø¬ Ø¥ÙÙ Ø§ÙØªØ¨Ø§ÙÙ - Ø±Ø§Ø¬Ø¹ Ø§ÙØªÙØ§ØµÙÙ Ø£Ø¯ÙØ§Ù.","ÙÙÙ .mc4 Ø£Ù .shn Ø£Ù .json ÙÙ Ø¬ÙØ§Ø² Ø§ÙÙÙØ¨ÙÙØªØ±.","Ø£Ø¶Ù ÙÙÙÙØ§ ÙÙ Ø§ÙÙÙÙØ§Øª Ø£Ù Ø§ÙÙØµØ§Ø¯Ø±","Ø§ÙÙØ¸ÙØ±","Ø§ÙÙØ¸ÙØ± ÙÙØ­Ø±Ù Ø§ÙØºØ´ ÙØ®ÙØ§Ø±Ø§Øª Ø°Ø±Ø§Ø¹ Ø§ÙØªØ­ÙÙ. ØªÙØ­ÙØ¸ Ø§ÙØªØºÙÙØ±Ø§Øª ÙÙØ±ÙØ§.","ÙÙÙ Ø§ÙØºØ´","ÙÙÙØ§Øª Ø§ÙØºØ´ ÙÙØ°Ù Ø§ÙÙØ¹Ø¨Ø©","ÙØ¹Ø±Ø¶ CheatRunner Ø§ÙØ£ÙØ¹Ø§Ø¨ Ø§ÙÙØ«Ø¨ÙØªØ© Ø¹ÙÙ ÙØ°Ø§ Ø§ÙØ¬ÙØ§Ø². Ø«Ø¨ÙØª ÙØ¹Ø¨Ø© Ø«Ù Ø­Ø¯ÙØ« Ø§ÙÙØ§Ø¦ÙØ©.","ÙØ§ ÙÙÙÙ ØªØ·Ø¨ÙÙ Ø§ÙØºØ´ ÙØ§ÙØªØµØ­ÙØ­Ø§Øª Ø­ØªÙ ÙØªÙ ØªØ´ØºÙÙÙ.","Ø¬Ø§Ø±Ù Ø§ÙÙØ­Øµ...","Ø§Ø®ØªÙØ§Ø± ÙÙÙ Ø¢Ø®Ø±","ØªØ¹Ø°ÙØ± Ø§ÙØªØ­ÙÙÙ","ØªØ¹Ø°ÙØ± ØªØ­ÙÙÙ ÙÙÙ Ø§ÙØºØ´","ØªØ¹Ø°ÙØ± ØªØ­ÙÙÙ Ø§ÙØªØµØ­ÙØ­Ø§Øª","ØªØ¹Ø°ÙØ± ØªØ­ÙÙÙ Ø§ÙØ¥Ø¹Ø¯Ø§Ø¯Ø§Øª","ØªØ¹Ø°ÙØ± ØªØ­ÙÙÙ Ø£ÙØ¹Ø§Ø¨Ù","ØªØ¹Ø°ÙØ±Øª ÙØ±Ø§Ø¡Ø© Ø§ÙØ­Ø§ÙØ© Ø§ÙØ­Ø§ÙÙØ© ÙÙØºØ´.","ÙØ²ÙÙ ØºØ´ Ø§ÙÙØ¬ØªÙØ¹ ÙÙ Ø§ÙÙØµØ§Ø¯Ø± Ø£Ù Ø£Ø¶Ù ÙÙÙÙØ§ ÙÙ Ø¬ÙØ§Ø² Ø§ÙÙÙØ¨ÙÙØªØ±.","ÙØ²ÙÙ ÙØ§Ø­Ø¯ÙØ§ ÙÙ Ø§ÙÙØµØ§Ø¯Ø± Ø£Ù Ø£Ø³ÙØ· ÙÙÙÙØ§ Ø£Ø¯ÙØ§Ù.","ÙØ²ÙÙ ØºØ´ÙØ§ ÙØªØµØ­ÙØ­Ø§Øª Ø¬Ø§ÙØ²Ø© ÙØ£ÙØ¹Ø§Ø¨Ù. ØªØ³ØªÙØ± Ø§ÙØªÙØ²ÙÙØ§Øª Ø£Ø«ÙØ§Ø¡ ØªØµÙØ­Ù.","ÙØ²ÙÙ ÙØ¬ÙÙØ¹Ø© Ø§ÙØªØµØ­ÙØ­Ø§Øª Ø§ÙØ±Ø³ÙÙØ© ÙÙ Ø§ÙÙØµØ§Ø¯Ø± Ø£Ù Ø¶Ø¹ ØªØµØ­ÙØ­Ø§Øª XML ÙÙ /data/cheatrunner/patches/xml.","Ø¬Ø§Ø±Ù Ø§ÙØªÙØ²ÙÙ","Ø¬Ø§Ø±Ù Ø§ÙØªÙØ²ÙÙ {p}%","Ø§ÙØªÙØ²ÙÙØ§Øª","Ø§ÙÙØ­Ø±Ù","ÙØªØ§Ø¨Ø¹ Ø¢Ø®Ø± Ø§ÙØ£Ø³Ø·Ø±","Ø§ÙØ­ØµÙÙ Ø¹ÙÙ Ø§ÙØºØ´","Ø§ÙØ­Ø§ÙØ©","Ø§ÙÙÙØªØ¨Ø©","Ø§ÙØ­Ø§ÙØ© Ø§ÙÙØ¨Ø§Ø´Ø±Ø© ÙÙØ­Ø±Ù Ø§ÙØºØ´ ÙØªØ¹Ø¯ÙÙ Ø§ÙØ°Ø§ÙØ±Ø©Ø ÙÙØ§ ÙØ¹ÙÙ CheatRunner.","Ø£Ø®Ø¨Ø§Ø± ÙÙØ³Ø§Ø¹Ø¯Ø© ÙØ§ÙØ£Ø´Ø®Ø§Øµ Ø®ÙÙ CheatRunner.","ÙØ§ ÙÙØ¬Ø¯ ÙÙÙ ØºØ´ ÙÙØ°Ù Ø§ÙÙØ¹Ø¨Ø© Ø¨Ø¹Ø¯","ÙØ§ ÙÙØ¬Ø¯ ØºØ´ ÙØ·Ø§Ø¨Ù \"{q}\".","ÙØ§ ÙÙØ¬Ø¯ ÙÙÙ Ø¨Ø¹Ø¯","ÙÙ ÙØªÙ Ø§ÙØ¹Ø«ÙØ± Ø¹ÙÙ Ø£ÙØ¹Ø§Ø¨ Ø¨Ø¹Ø¯","ÙÙ ÙØªÙ Ø§ÙØ¹Ø«ÙØ± Ø¹ÙÙ ÙÙÙ ÙØ­ÙÙ","ÙØ§ ØªÙØ¬Ø¯ ØªØµØ­ÙØ­Ø§Øª ÙÙØ°Ù Ø§ÙÙØ¹Ø¨Ø©","ÙÙØ¯ Ø§ÙÙØ¹Ø¨ Ø§ÙØ¢Ù","ÙØªØ­ Ø§ÙØ¥Ø¹Ø¯Ø§Ø¯Ø§Øª","ÙØªÙÙÙ ÙØ¤ÙØªÙØ§ Ø£Ø«ÙØ§Ø¡ Ø§ÙÙØ±Ø§Ø¡Ø©","Ø§ÙØªÙØ¶ÙÙØ§Øª","Ø®ÙØ§Ø± Â«ÙØ®ÙÙÙØ©Â» ÙØ²ÙÙ ÙÙ Ø§ÙØ­Ø±ÙØ§Øª. ÙÙÙ Ø§ÙØ§ÙØªØ±Ø§Ø¶Ù ÙÙ ÙØªØµÙØ­ PS5.","ØªÙØ­ÙØ¸ ÙÙ /data/cheatrunner/cheats. Ø§ÙÙÙÙ Ø§ÙÙØ¹ÙÙÙÙ Â«ÙÙØ¯ Ø§ÙØ§Ø³ØªØ®Ø¯Ø§ÙÂ» ÙÙ Ø§ÙØ°Ù ÙÙØ±Ø¤Ù Ø§ÙÙØ¯Ø±Ø¨.","Ø£Ø±Ø³Ù Ø¨ÙØ§ØºÙØ§ Ø¹ÙØ¯ÙØ§ ÙØ§ ÙØ¹ÙÙ Ø´ÙØ¡.","Ø´ØºÙÙÙØ§ ÙØ§Ø³ØªØ®Ø¯Ø§Ù Ø§ÙØºØ´","Ø´ØºÙÙ ÙØ°Ù Ø§ÙÙØ¹Ø¨Ø© ÙØ§Ø³ØªØ®Ø¯Ø§Ù Ø§ÙØºØ´ Ø§ÙØ®Ø§Øµ Ø¨ÙØ§.","Ø§ÙØªÙØ§ØµÙÙ Ø§ÙØªÙÙÙØ©","ÙØ­Ø±Ù Ø§ÙØºØ´ ÙØªÙÙÙ.","ØªØ¹Ø°ÙØ±Øª ÙØ±Ø§Ø¡Ø© Ø§ÙÙÙÙ Ø§ÙØ­Ø§ÙÙØ©Ø ÙØ°ÙÙ ÙØ§ ÙÙØ¹Ø±Ø¶ Ø´ÙØ¡ Ø¨Ø¯Ù Ø¹Ø±Ø¶ ÙÙÙ Ø®Ø§Ø·Ø¦Ø©.","Ø§ÙØ£Ø³Ø§Ø³ÙØ§Øª ÙØªØ·Ø¨ÙÙ Ø§ÙØºØ´ ÙØ§ÙØªØµØ­ÙØ­Ø§Øª.","Ø§ÙÙÙÙ ÙÙØ¬ÙØ¯ ÙÙÙ ØªØ¹Ø°ÙØ±Øª ÙØ±Ø§Ø¡ØªÙ. ÙØ¯ ÙÙÙÙ ØªØ§ÙÙÙØ§ Ø£Ù ÙØ§ Ø²Ø§Ù ÙÙØ¯ Ø§ÙÙØ³Ø®.","ØªÙ ØªØ­ÙÙÙ Ø§ÙÙÙÙ ÙÙÙÙ ÙØ§Ø±Øº. Ø¬Ø±ÙØ¨ ÙÙÙÙØ§ Ø¢Ø®Ø± ÙÙØ°Ù Ø§ÙÙØ¹Ø¨Ø©.","ÙØ§Ø¦ÙØ© Ø§ÙØ£ÙØ¹Ø§Ø¨ ØºÙØ± ÙØªØ§Ø­Ø© Ø§ÙØ¢Ù. ØªØ£ÙØ¯ ÙÙ Ø£Ù Ø§ÙØ¬ÙØ§Ø² ÙØ¹ÙÙ Ø«Ù Ø­Ø§ÙÙ ÙØ¬Ø¯Ø¯ÙØ§.","ØªØ¹Ø°ÙØ±Øª ÙØ±Ø§Ø¡Ø© ÙÙÙØ§Øª Ø§ÙØªØµØ­ÙØ­Ø§Øª ÙÙØ°Ù Ø§ÙÙØ¹Ø¨Ø©. Ø­Ø§ÙÙ ÙØ¬Ø¯Ø¯ÙØ§ Ø¨Ø¹Ø¯ ÙÙÙÙ.","ÙØ¯ ÙØ§ ØªÙÙÙ Ø§ÙÙÙØ§ØªÙØ­ ÙØ­Ø¯ÙØ«Ø©.","Ø§ÙØ³ÙØ© ÙØ§ÙØªØ£Ø«ÙØ±Ø§Øª ÙÙØºØ© ÙØ°Ù Ø§ÙÙÙØ­Ø©.","ÙØ§ ÙØ­ØªÙÙ ÙÙÙ Ø§ÙØºØ´ ÙØ°Ø§ Ø¹ÙÙ Ø£Ù ØºØ´","Ø­Ø§ÙÙ ÙØ¬Ø¯Ø¯ÙØ§ Ø£Ø¯ÙØ§Ù","Ø´ØºÙÙÙ ÙÙ Ø§ÙØ¥Ø¹Ø¯Ø§Ø¯Ø§Øª","ÙÙØ·ÙØ© Ø§ÙØ®Ø·Ø±","Ø§ÙØ§ÙØªÙØ§Ù Ø¥ÙÙ Ø§ÙØ£Ø­Ø¯Ø«","Ø­Ø°Ù Ø§ÙÙÙÙ ÙÙØ¯ Ø§ÙØ§Ø³ØªØ®Ø¯Ø§Ù","ÙØµØ¯Ø± Ø§ÙØºØ´","ØªØµÙÙØ© Ø§ÙØ£ÙØ¹Ø§Ø¨","Ø¹Ø±Ø¶ Ø£ÙØ¹Ø§Ø¨Ù","Ø§ÙÙÙØªÙØ§Ø¨","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"de-DE":["Ein Cheat hat das Spiel beim letzten Mal abstÃ¼rzen lassen.","Info","Aktion","Aktionen","Datei hinzufÃ¼gen","Cheats hinzufÃ¼gen","Zu Favoriten hinzufÃ¼gen","FÃ¼gt jedem Spiel im OptionsmenÃ¼ einen â CheatRunner-Eintrag hinzu sowie eine Tastenkombination, die den Trainer des laufenden Spiels Ã¶ffnet.","Erweitert","Alle","Alle BeitrÃ¤ge flieÃen in unser Ziel, eine PS5 mit niedriger Firmware (1.xxâ4.xx) fÃ¼r Forschung, Entwicklung und zukÃ¼nftige Erweiterungen anzuschaffen.","Alle Downloads sind fehlgeschlagen","Alle Spiele","Auch geÃ¤nderte Dateien aktualisieren","Angewendet","Anwenden","Apps","Automatisch wÃ¤hlen","ZurÃ¼ck","Bibliothek durchsuchen","Tasten","Buy Me a Coffee","Cheat","Cheat-Engine","CheatRunner - Cheat-Trainer fÃ¼r deine PS5!","Cheats","Cheats werden nach dem Spielstart so lange zurÃ¼ckgehalten.","Mit Cheats zuerst","Netzwerk wÃ¤hlen","Circle","Filter lÃ¶schen","Markierung lÃ¶schen","Suche lÃ¶schen","SchlieÃen","Einklappen","MenÃ¼ einklappen","Farbschema dieses Dashboards.","Community","Community-Cheat-Repositories","Controller-Shortcut und SpielmenÃ¼","Kopieren","URL kopieren","Alles kopieren","Cheat-Ordner kopieren","Link kopieren","Absturzerkennung","Hat das Spiel zuletzt abstÃ¼rzen lassen","Cross","Krypto","Krypto-UnterstÃ¼tzung","Dunkel","LÃ¶schen","Alle deaktivieren","Unten","Alle Quellen herunterladen","Community-Cheats herunterladen","Download fehlgeschlagen","Lade fertige Cheat-Dateien fÃ¼r deine Spiele herunter. Sie werden in /data/cheatrunner/cheats gespeichert und Downloads laufen im Hintergrund weiter.","Auswahl herunterladen","Heruntergeladen","Lege eine .mc4-, .shn- oder .json-Datei hier ab oder klicke zum Durchsuchen","Controller-Shortcut aktivieren","Shortcut aktivieren","Engine an","Enter Ã¶ffnet","Nur Fehler","Exakt","Fehlgeschlagen","Favoriten","Dateien","Filtern","Protokoll filtern","Einen Cheat markieren, wenn das Spiel kurz nach dem Aktivieren beendet wird.","Von Nutzern. FÃ¼r Nutzer.","Erzwingen","Spiele","Spiele mit Cheats","Spiele mit Patches","Abstand zwischen Cheats (ms)","Allgemein","Gemeinsam gedrÃ¼ckt, um den Shortcut auszulÃ¶sen.","Haltezeit (ms)","Halte zwei Tasten gleichzeitig gedrÃ¼ckt, um den Trainer des laufenden Spiels zu Ã¶ffnen.","Startbildschirm-Kachel","Greift in die SystemoberflÃ¤che ein. StandardmÃ¤Ãig aus â bei manchen Firmwares kann der Startbildschirm abstÃ¼rzen.","Wie lange die Kombination gehalten werden muss, bis sie auslÃ¶st.","Wie detailliert das Protokoll geschrieben wird.","Wenn dir CheatRunner nÃ¼tzlich war, unterstÃ¼tze gern die Entwicklung des Projekts! ð«¶","In Benutzung","Installiert die CheatRunner-Kachel beim Start auf dem PS5-Startbildschirm.","Dem Discord beitreten","Ko-Fi, Kaffee, Krypto, PIX","Sprache","Sprache dieses Dashboards.","Zuletzt gespielt","Links","Hell","Zustand der Cheat-Engine und des Speicher-Patchings.","Lokale Cheat-Dateien","Protokollstufe","Protokollstufen-Filter","Protokolle","HauptmenÃ¼","Hauptschalter fÃ¼r das Anwenden von Cheats und Patches.","Master-Code-Korrektur","Passt eventuell nicht zu dieser Spielversion","Speicher-Patching","Mitternacht","Mindestzeit zwischen zwei Cheat-Umschaltungen.","Name A-Z","Name Z-A","FÃ¼r dieses Spiel gibt es noch keine Cheat-Datei.","Noch keine Favoriten. Tippe bei einem Spiel auf den Stern.","LÃ¤uft nicht","An","Auf dem Handy Ã¶ffnen","Auf dem Handy Ã¶ffnen (QR-Code)","Dashboard auf dem Handy Ã¶ffnen","Trainer Ã¶ffnen","Optionen","Andere Version","Seite","Patches","Spielen","QR-Code","QR-Code fÃ¼r die Dashboard-Adresse","Aktive Cheats bei jedem Start dieses Spiels erneut anwenden","Bereit","Zuletzt","Aktualisieren","Spieleliste aktualisieren","Neu starten","Problem melden","Installierte Spiele neu einlesen","Auf Standard zurÃ¼cksetzen","Master-Code-abhÃ¤ngige Cheats automatisch auflÃ¶sen.","Wiederherstellen","Rechts","LÃ¤uft","LÃ¤uft gerade","Autoload speichern","Scanne den Code mit der Handykamera. Das Handy muss im selben WLAN wie die PS5 sein.","Suchen","Nach Name oder Titel-ID suchen","Spiele suchen","Spiele, Seiten und Aktionen suchen","WÃ¤hle eine Adresse, um sie vollstÃ¤ndig zu kopieren.","Einstellungen","QR-Code anzeigen","Payload beenden","Spiele sortieren","Quellcode","Quellen","Geschwindigkeit","Square","Spiel starten","Starte dieses Spiel, um Patches anzuwenden.","Status","Status und Protokolle","CheatRunner unterstÃ¼tzen","UnterstÃ¼tzung aus Brasilien","Dieses Projekt unterstÃ¼tzen","UnterstÃ¼tzer","System","Das Wesentliche â Design und Schalter der Cheat-Engine.","Design","Design und Cheat-Engine","Verbleibend","Spielzeit","Titel-ID","Schalter","TouchPad","Trainer","Triangle","Oben","Aktuell","Verwenden","Version","Wartezeit nach Spielstart (ms)","Warnungen und Fehler","Was CheatRunner getan hat â nÃ¼tzlich, wenn etwas nicht funktioniert.","Mit Cheats","ââ navigieren","Alle Einstellungen werden auf Standard zurÃ¼ckgesetzt. Der Web-Port bleibt erhalten.","Ein anderer Cheat wird gerade angewendet. Versuche es gleich noch einmal.","Jede Version","Trotzdem anwenden","Automatische Auswahl wiederhergestellt.","Autoload gelÃ¶scht.","Autoload gespeichert ({n}). Sie werden beim Spielstart aktiviert.","Leerer Autoload gespeichert.","Cheat Nr. {n}","Cheat-Datei hinzugefÃ¼gt.","Cheat-Datei gelÃ¶scht.","Cheat-Datei ausgewÃ¤hlt.","CheatRunner hat beim Start nicht mehr reagiert.","Cheat-Ordner kopiert.","{name} wird geprÃ¼ft","Autoload lÃ¶schen ({n})","Dateien werden einzeln verglichen und geladen","Steht im Konflikt mit einem anderen Cheat","Kopieren fehlgeschlagen. Markiere den Text manuell.","Autoload konnte nicht gelÃ¶scht werden.","Diagnose konnte nicht kopiert werden.","Cheats konnten nicht deaktiviert werden.","Patches konnten nicht geladen werden.","Spieleliste konnte nicht geladen werden.","Autoload konnte nicht gespeichert werden.","Speichern fehlgeschlagen. Versuche es erneut.","Datei konnte nicht ausgewÃ¤hlt werden.","Download konnte nicht gestartet werden.","Start nicht mÃ¶glich.","Cheat konnte nicht umgeschaltet werden.","LÃ¶schen fehlgeschlagen.","Datei lÃ¶schen","{file} lÃ¶schen?","Diagnose kopiert. FÃ¼ge sie in das Issue ein.","{name} wird heruntergeladen","Engine aus","Alles ist bereits aktuell","MenÃ¼ ausklappen","Das gesamte Repository wird auf einmal geladen","Datei v{a} / Spiel v{b}","{name} abgeschlossen","{name} erzwingen?","Spiel gestartet.","Er zielt auf eine andere Spielversion und kann das Spiel abstÃ¼rzen lassen. Wiederherstellen setzt die Originalbytes zurÃ¼ck.","Start fehlgeschlagen.","Start fehlgeschlagen: {msg}","Startanfrage fehlgeschlagen. Das Spiel startet mÃ¶glicherweise noch.","ZeitÃ¼berschreitung beim Start. PrÃ¼fe, ob das Spiel gestartet ist.","{name} wird gestartet...","Protokolle kopiert.","Mit {heart} von maj0r gemacht","Passt zu v{v}","BenÃ¶tigt v{v}","Kein Spiel","Keine Spiele passen zu deiner Suche.","Noch keine Spiele mit Cheats. Hol dir welche unter Quellen.","Noch keine Protokolle.","Keine passenden Zeilen.","Nicht bereit","Nichts Neues. Starte ein Spiel oder Ã¶ffne dessen Trainer.","Nichts zu kopieren.","Nichts war aktiv.","Trainer Ã¶ffnen: {name}","Patch-Autoload gelÃ¶scht.","Patch-Autoload gespeichert.","QR-Code ist nicht verfÃ¼gbar.","Dateiliste wird von GitHub gelesen","Aus Favoriten entfernen","ZurÃ¼cksetzen fehlgeschlagen.","Einstellungen zurÃ¼cksetzen?","Gespeichert","Cheat-Dateien werden gespeichert","Speichern...","Sende danach eine neue CheatRunner.elf von deinem PC.","Einstellungen zurÃ¼ckgesetzt.","Beenden","CheatRunner beenden?","Quelle {i} von {n}","Starte zuerst das Spiel.","Das Spiel lÃ¤dt noch.","Das Spiel startet noch. Versuche es in ein paar Sekunden erneut.","Die Spieleliste ist gerade nicht verfÃ¼gbar.","Dieser Cheat kann gerade nicht umgeschaltet werden.","Dieser Cheat hat das Spiel schon einmal abstÃ¼rzen lassen. LÃ¶sche die Markierung, um es erneut zu versuchen.","Diese Datei gehÃ¶rt zu einer anderen Spielversion als der installierten.","Dadurch wird die Datei von der Konsole entfernt. Du kannst sie unter Quellen erneut herunterladen.","Dauer: {t}","URL kopiert.","{name} wird entpackt","Nicht unterstÃ¼tzter Patch","Hochladen fehlgeschlagen.","Verwende eine .mc4-, .shn- oder .json-Datei.","Andere Version verwenden?","Trotzdem verwenden","Warte, bis CheatRunner zurÃ¼ckkommt...","{label}-Adresse kopiert","alle Quellen","Ã {s}","gerade eben","die Cheat-Datei","v{v} passt","{n} Cheat(s) deaktiviert.","{n} neue oder aktualisierte Datei(en) gespeichert","{n} Titel","{n} Titel","vor {n} T.","vor {n} Std.","vor {n} Min.","{page} - CheatRunner","{a} von {n} an","{h} Std. {m} Min.","{m} Min.","{s} Sek.","{m} Min. {s} Sek.","von {a}","Willkommen bei CheatRunner.","Noch keine Cheat-Dateien. Hole Community-Cheats unter Quellen oder Ã¶ffne ein Spiel und lege eine Datei ab.","Abbrechen","Weiter","Verbindung wird wiederhergestellt...","Kopiert","Kopiert.","Unbekannt","Dialog","Aus","Berechtigungen","OK","Fehlt","Fehlgeschlagen.","Starte","Erneut versuchen","Spiel","Cheats suchen","Folge maj0r auf X","Folge maj0r auf Bluesky","Von maj0r - ð§ð·","Gib dem Projekt einen Stern! â­","Karmesin","Autoload","ZurÃ¼cksetzen","Keine Patches fÃ¼r dieses Spiel.","Lege deine XML-Patches in /data/cheatrunner/patches/xml ab","Update verfÃ¼gbar","Version {v} ist verfÃ¼gbar.","Cheats online durchsuchen","Durchsuche alle Spiele mit Cheats und prÃ¼fe, ob deines dabei ist.","Seite Ã¶ffnen","Community-Patch-Repositories","Fertige Patches fÃ¼r deine Spiele. Es wird nichts heruntergeladen â das Repository Ã¶ffnet sich in einem neuen Tab.","PS-Game-Patch Ã¶ffnen","Visuelle Effekte","Reduziert schaltet UnschÃ¤rfe, Schatten und Animationen ab. Im PS5-Browser ist das die Voreinstellung.","Voll","Reduziert","Mehr anzeigen ({n})","Empfohlen","Offizielles PS-Game-Patch-Release. Die Patches werden in /data/cheatrunner/patches gespeichert.","Patches herunterladen","Patch-Dateien werden gespeichert","Manche Patches wirken nur, wenn sie beim Spielstart angewendet werden: Autoload speichern und das Spiel neu starten.","Fehlerbehebung","Ein Problem melden oder alle Einstellungen auf den Standard zurÃ¼cksetzen.","Kopiert die Diagnosedaten und Ã¶ffnet die Issue-Seite auf GitHub.","Cheats, Patches und Autoload-Profile bleiben unberÃ¼hrt.","Alles sieht gut aus.","Etwas braucht Aufmerksamkeit - siehe die Details unten.","Eine .mc4-, .shn- oder .json-Datei von deinem Computer.","FÃ¼ge eine unter Dateien oder Quellen hinzu","Darstellung","Darstellung, Cheat-Engine und Controller. Ãnderungen werden sofort gespeichert.","Cheat-Datei","Cheat-Dateien fÃ¼r dieses Spiel","CheatRunner listet die auf dieser Konsole installierten Spiele auf. Installiere eines und aktualisiere die Liste.","Cheats und Patches lassen sich erst anwenden, wenn sie eingeschaltet ist.","Wird geprÃ¼ft...","Andere Datei wÃ¤hlen","Laden fehlgeschlagen","Die Cheat-Datei konnte nicht geladen werden","Die Patches konnten nicht geladen werden","Die Einstellungen konnten nicht geladen werden","Deine Spiele konnten nicht geladen werden","Der aktuelle Cheat-Status konnte nicht gelesen werden.","Lade Community-Cheats unter Quellen herunter oder fÃ¼ge eine Datei von deinem Computer hinzu.","Lade eines unter Quellen herunter oder ziehe unten eine Datei hinein.","Lade fertige Cheats und Patches fÃ¼r deine Spiele herunter. Downloads laufen weiter, wÃ¤hrend du stÃ¶berst.","Lade das offizielle Patch-Paket unter Quellen herunter oder lege deine XML-Patches in /data/cheatrunner/patches/xml ab.","Download lÃ¤uft","Download {p}%","Downloads","Engine","Folgt den neuesten Zeilen","Cheats holen","Zustand","Bibliothek","Live-Zustand von Cheat-Engine und Speicher-Patching sowie die bisherigen AktivitÃ¤ten von CheatRunner.","Neuigkeiten, Hilfe und die Menschen hinter CheatRunner.","Noch keine Cheat-Datei fÃ¼r dieses Spiel","Kein Cheat passt zu â{q}â.","Noch keine Datei","Noch keine Spiele gefunden","Keine lokale Datei gefunden","Keine Patches fÃ¼r dieses Spiel","LÃ¤uft gerade","Einstellungen Ã¶ffnen","Pausiert, solange du liest","Einstellungen","âReduziertâ entfernt alle Animationen. Das ist der Standard im PS5-Browser.","Gespeichert in /data/cheatrunner/cheats. Die als âIn Benutzungâ markierte Datei liest der Trainer.","Sende einen Bericht, wenn etwas nicht funktioniert.","Starte es, um Cheats zu nutzen","Starte dieses Spiel, um seine Cheats zu nutzen.","Technische Details","Die Cheat-Engine ist aus.","Die aktuellen Werte konnten nicht gelesen werden. Lieber zeigen wir nichts an als falsche Werte.","Das Wichtigste zum Anwenden von Cheats und Patches.","Die Datei ist vorhanden, konnte aber nicht gelesen werden. Sie ist vielleicht beschÃ¤digt oder wird noch kopiert.","Die Datei wurde geladen, ist aber leer. Probiere eine andere Datei fÃ¼r dieses Spiel.","Die Spieleliste ist gerade nicht verfÃ¼gbar. PrÃ¼fe, ob die Konsole aktiv ist, und versuche es erneut.","Die Patch-Dateien fÃ¼r dieses Spiel konnten nicht gelesen werden. Versuche es gleich noch einmal.","Die Schalter sind mÃ¶glicherweise nicht aktuell.","Design, Effekte und Sprache dieses Dashboards.","Diese Cheat-Datei enthÃ¤lt keine Cheats","Unten erneut versuchen","In den Einstellungen einschalten","Gefahrenbereich","Zum Neuesten springen","Verwendete Datei lÃ¶schen","Cheat-Quelle","Spiele filtern","Deine Spiele ansehen","Credits","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"es-ES":["Un cheat cerrÃ³ el juego la Ãºltima vez.","Acerca de","AcciÃ³n","Acciones","AÃ±adir un archivo","AÃ±adir cheats","AÃ±adir a favoritos","AÃ±ade una entrada â CheatRunner al menÃº de opciones de cada juego y una combinaciÃ³n de botones que abre el trainer del juego en ejecuciÃ³n.","Avanzado","Todos","Todas las contribuciones irÃ¡n destinadas a nuestro objetivo de adquirir una PS5 con firmware bajo (1.xxâ4.xx) para investigaciÃ³n, desarrollo y futuras mejoras.","Todas las descargas fallaron","Todos los juegos","Actualizar tambiÃ©n los archivos que cambiaron","Aplicado","Aplicar","Apps","SelecciÃ³n automÃ¡tica","AtrÃ¡s","Explora tu biblioteca","Botones","Buy Me a Coffee","Cheat","Motor de cheats","CheatRunner - Â¡Cheat Trainer para tu PS5!","Cheats","Los cheats se retrasan este tiempo despuÃ©s de iniciar un juego.","Con cheats primero","Elige una red","Circle","Borrar filtros","Quitar marca","Borrar bÃºsqueda","Cerrar","Contraer","Contraer menÃº","Esquema de colores de este panel.","Comunidad","Repositorios de cheats de la comunidad","Atajo del mando y menÃº del juego","Copiar","Copiar URL","Copiar todo","Copiar carpeta de cheats","Copiar enlace","DetecciÃ³n de fallos","CerrÃ³ el juego la Ãºltima vez","Cross","Cripto","Apoyo con cripto","Oscuro","Eliminar","Desactivar todos","Abajo","Descargar todas las fuentes","Descarga cheats de la comunidad","Descarga fallida","Descarga archivos de cheats listos para tus juegos. Se guardan en /data/cheatrunner/cheats y las descargas continÃºan en segundo plano.","Descargar seleccionado","Descargado","Suelta un archivo .mc4, .shn o .json aquÃ­, o haz clic para buscar","Activar atajo del mando","Activar atajo","Motor activo","Intro abre","Solo errores","Exacta","Fallido","Favoritos","Archivos","Filtrar","Filtrar registros","Marcar un cheat si el juego se cierra justo despuÃ©s de activarlo.","Para usuarios. Por usuarios.","Forzar aplicaciÃ³n","Juegos","Juegos con cheats","Juegos con parches","Intervalo entre cheats (ms)","General","Pulsados a la vez para activar el atajo.","Tiempo de pulsaciÃ³n (ms)","MantÃ©n dos botones a la vez para abrir el trainer del juego en ejecuciÃ³n.","Acceso en la pantalla de inicio","Se engancha a la interfaz del sistema. Desactivado por defecto: en algunos firmwares puede colgar la pantalla de inicio.","CuÃ¡nto mantener la combinaciÃ³n antes de que se active.","CuÃ¡nto detalle se escribe en el registro.","Si CheatRunner te resultÃ³ Ãºtil, Â¡considera apoyar el desarrollo del proyecto! ð«¶","En uso","Instala el icono de CheatRunner en la pantalla de inicio de la PS5 al arrancar.","Unirse al Discord","Ko-Fi, cafÃ©, cripto, PIX","Idioma","Idioma de este panel.","Ãltima partida","Izquierda","Claro","Estado del motor de cheats y de la escritura en memoria.","Archivos de cheats locales","Nivel de registro","Filtro de nivel de registro","Registros","Principal","Interruptor general para aplicar cheats y parches.","CorrecciÃ³n de master code","Puede no coincidir con esta versiÃ³n del juego","Parcheo de memoria","Medianoche","Tiempo mÃ­nimo entre dos activaciones de cheats.","Nombre A-Z","Nombre Z-A","AÃºn no hay archivo de cheats para este juego.","AÃºn no hay favoritos. Toca la estrella de un juego para aÃ±adirlo.","No se estÃ¡ ejecutando","Activado","Abrir en el mÃ³vil","Abrir en el mÃ³vil (cÃ³digo QR)","Abre el panel en tu mÃ³vil","Abrir trainer","Opciones","Otra versiÃ³n","PÃ¡gina","Parches","Jugar","CÃ³digo QR","CÃ³digo QR de la direcciÃ³n del panel","Volver a aplicar los cheats ACTIVADOS cada vez que se inicie este juego","Listo","Recientes","Actualizar","Actualizar lista de juegos","Reiniciar","Informar de un problema","Volver a buscar juegos instalados","Restablecer valores","Resolver automÃ¡ticamente los cheats que dependen de un master code.","Restaurar","Derecha","En ejecuciÃ³n","En ejecuciÃ³n ahora","Guardar autocarga","Escanea esto con la cÃ¡mara del mÃ³vil. El mÃ³vil debe estar en la misma Wi-Fi que la PS5.","Buscar","Buscar por nombre o ID de tÃ­tulo","Buscar juegos","Buscar juegos, pÃ¡ginas y acciones","Selecciona una direcciÃ³n para copiarla completa.","Ajustes","Mostrar cÃ³digo QR","Cerrar el payload","Ordenar juegos","CÃ³digo fuente","Fuentes","Velocidad","Square","Inicia el juego","Inicia este juego para aplicar parches.","Estado","Estado y registros","Apoya CheatRunner","Apoyo desde Brasil","Apoya este proyecto","Colaboradores","Sistema","Lo esencial: tema e interruptor del motor de cheats.","Tema","Tema y motor de cheats","Tiempo restante","Tiempo jugado","ID de tÃ­tulo","Interruptores","TouchPad","Trainer","Triangle","Arriba","Actualizados","Usar","VersiÃ³n","Espera tras iniciar el juego (ms)","Avisos y errores","Lo que ha estado haciendo CheatRunner: Ãºtil cuando algo no funciona.","Con cheats","ââ navegar","Todos los ajustes vuelven a sus valores por defecto. Se mantiene el puerto web.","Se estÃ¡ aplicando otro cheat. IntÃ©ntalo de nuevo en un momento.","Cualquier versiÃ³n","Aplicar de todos modos","SelecciÃ³n automÃ¡tica restaurada.","Autocarga eliminada.","Autocarga guardada ({n}). Se activan al iniciar el juego.","Autocarga guardada vacÃ­a.","Cheat n.Âº {n}","Archivo de cheats aÃ±adido.","Archivo de cheats eliminado.","Archivo de cheats seleccionado.","CheatRunner dejÃ³ de responder durante el inicio.","Carpeta de cheats copiada.","Comprobando {name}","Borrar autocarga ({n})","Comparando y descargando archivos uno a uno","Entra en conflicto con otro cheat","Error al copiar. Selecciona el texto manualmente.","No se pudo borrar la autocarga.","No se pudo copiar el diagnÃ³stico.","No se pudieron desactivar los cheats.","No se pudieron cargar los parches.","No se pudo cargar la lista de juegos.","No se pudo guardar la autocarga.","No se pudo guardar. IntÃ©ntalo de nuevo.","No se pudo seleccionar el archivo.","No se pudo iniciar la descarga.","No se pudo iniciar.","No se pudo alternar el cheat.","Error al eliminar.","Eliminar archivo","Â¿Eliminar {file}?","DiagnÃ³stico copiado. PÃ©galo en el informe del problema.","Descargando {name}","Motor apagado","Todo ya estÃ¡ actualizado","Expandir menÃº","Obteniendo todo el repositorio de una vez","Archivo v{a} / juego v{b}","{name} terminado","Â¿Forzar la aplicaciÃ³n de {name}?","Juego iniciado.","EstÃ¡ pensado para otra versiÃ³n del juego y puede cerrar el juego. Restaurar devuelve los bytes originales.","Error al iniciar.","Error al iniciar: {msg}","La solicitud de inicio fallÃ³. Puede que el juego aÃºn se estÃ© iniciando.","Tiempo de inicio agotado. Comprueba si el juego se abriÃ³.","Iniciando {name}...","Registros copiados.","Hecho con {heart} por maj0r","Coincide con v{v}","Requiere v{v}","NingÃºn juego","NingÃºn juego coincide con tu bÃºsqueda.","AÃºn no hay juegos con cheats. Consigue algunos en Fuentes.","AÃºn no hay registros.","Ninguna lÃ­nea coincide.","No listo","Nada reciente. Inicia un juego o abre su trainer.","Nada que copiar.","No habÃ­a nada activado.","Abrir trainer: {name}","Autocarga de parches eliminada.","Autocarga de parches guardada.","El cÃ³digo QR no estÃ¡ disponible.","Leyendo la lista de archivos de GitHub","Quitar de favoritos","Error al restablecer.","Â¿Restablecer los ajustes?","Guardado","Guardando los archivos de cheats","Guardando...","DespuÃ©s, envÃ­a un nuevo CheatRunner.elf desde tu PC.","Ajustes restablecidos.","Cerrar","Â¿Cerrar CheatRunner?","Fuente {i} de {n}","Inicia el juego primero.","El juego aÃºn estÃ¡ cargando.","El juego aÃºn se estÃ¡ iniciando. IntÃ©ntalo de nuevo en unos segundos.","La lista de juegos no estÃ¡ disponible ahora.","Este cheat no se puede alternar ahora.","Este cheat ya cerrÃ³ el juego antes. Quita la marca para reintentar.","Este archivo es de una versiÃ³n del juego distinta a la instalada.","Esto elimina el archivo de la consola. Puedes volver a descargarlo desde Fuentes.","TardÃ³ {t}","URL copiada.","Descomprimiendo {name}","Parche no compatible","Error al subir.","Usa un archivo .mc4, .shn o .json.","Â¿Usar otra versiÃ³n?","Usar de todos modos","Esperando a que CheatRunner vuelva...","DirecciÃ³n {label} copiada","todas las fuentes","prom. {s}","justo ahora","el archivo de cheats","v{v} coincide","{n} cheat(s) desactivado(s).","{n} archivo(s) nuevo(s) o actualizado(s) guardado(s)","{n} tÃ­tulo","{n} tÃ­tulos","hace {n} d","hace {n} h","hace {n} min","{page} - CheatRunner","{a} de {n} activos","{h} h {m} min","{m} min","{s} s","{m} min {s} s","por {a}","Te damos la bienvenida a CheatRunner.","AÃºn no hay archivos de cheats. Descarga cheats de la comunidad en Fuentes o abre un juego y suelta un archivo.","Cancelar","Continuar","Reconectando...","Copiado","Copiado.","Desconocido","DiÃ¡logo","Desactivado","Privilegios","OK","Falta","Error.","Iniciando","Reintentar","Juego","Buscar cheats","Sigue a maj0r en X","Sigue a maj0r en Bluesky","Hecho por maj0r - ð§ð·","Â¡Deja una estrella al proyecto! â­","CarmesÃ­","Autocarga","Restablecer","No hay parches para este juego.","Coloca tus parches XML en /data/cheatrunner/patches/xml","ActualizaciÃ³n disponible","La versiÃ³n {v} estÃ¡ disponible.","Buscar cheats en lÃ­nea","Busca todos los juegos con cheats y comprueba si el tuyo estÃ¡ incluido.","Abrir sitio","Repositorios de parches de la comunidad","Parches listos para tus juegos. No se descarga nada: el repositorio se abre en una pestaÃ±a nueva.","Abrir PS-Game-Patch","Efectos visuales","Reducido desactiva el desenfoque, las sombras y las animaciones. Es el valor por defecto en el navegador de la PS5.","Completo","Reducido","Mostrar mÃ¡s ({n})","Recomendado","VersiÃ³n oficial de PS-Game-Patch. Los parches se guardan en /data/cheatrunner/patches.","Descargar parches","Guardando los archivos de parches","Algunos parches solo funcionan si se aplican al iniciar el juego: guarda la carga automÃ¡tica y reinicia el juego.","SoluciÃ³n de problemas","Informa de un problema o restablece todos los ajustes a sus valores por defecto.","Copia el diagnÃ³stico y abre la pÃ¡gina de incidencias en GitHub.","Los cheats, los parches y los perfiles de carga automÃ¡tica no se tocan.","Todo parece estar bien.","Algo requiere atenciÃ³n: revisa los detalles de abajo.","Un archivo .mc4, .shn o .json de tu ordenador.","AÃ±ade uno en Archivos o Fuentes","Apariencia","Apariencia, motor de cheats y mando. Los cambios se guardan al instante.","Archivo de cheats","Archivos de cheats de este juego","CheatRunner muestra los juegos instalados en esta consola. Instala uno y actualiza la lista.","No se pueden aplicar cheats ni parches hasta que se active.","Comprobando...","Elegir otro archivo","No se pudo cargar","No se pudo cargar el archivo de cheats","No se pudieron cargar los parches","No se pudieron cargar los ajustes","No se pudieron cargar tus juegos","No se pudo leer el estado actual de los cheats.","Descarga cheats de la comunidad en Fuentes o aÃ±ade un archivo de tu ordenador.","Descarga uno en Fuentes o suelta un archivo abajo.","Descarga cheats y parches listos para tus juegos. Las descargas siguen mientras navegas.","Descarga el conjunto oficial de parches en Fuentes o coloca tus parches XML en /data/cheatrunner/patches/xml.","Descargando","Descargando {p}%","Descargas","Motor","Siguiendo las Ãºltimas lÃ­neas","Obtener cheats","Estado","Biblioteca","Estado en vivo del motor de cheats y la ediciÃ³n de memoria, y lo que ha hecho CheatRunner.","Novedades, ayuda y las personas detrÃ¡s de CheatRunner.","AÃºn no hay archivo de cheats para este juego","NingÃºn cheat coincide con \"{q}\".","Sin archivo aÃºn","AÃºn no se encontraron juegos","No se encontrÃ³ ningÃºn archivo local","No hay parches para este juego","Jugando ahora","Abrir Ajustes","En pausa mientras lees","Preferencias","El modo reducido quita todas las animaciones. Es el valor por defecto en el navegador de PS5.","Guardados en /data/cheatrunner/cheats. El archivo marcado como En uso es el que lee el trainer.","EnvÃ­a un informe cuando algo no funcione.","InÃ­cialo para usar cheats","Inicia este juego para usar sus cheats.","Detalles tÃ©cnicos","El motor de cheats estÃ¡ apagado.","No se pudieron leer los valores actuales, asÃ­ que no se muestra nada para no mostrar valores errÃ³neos.","Lo esencial para aplicar cheats y parches.","El archivo existe, pero no se pudo leer. Puede estar daÃ±ado o aÃºn copiÃ¡ndose.","El archivo se cargÃ³, pero estÃ¡ vacÃ­o. Prueba con otro archivo para este juego.","La lista de juegos no estÃ¡ disponible ahora. Comprueba que la consola estÃ© activa e intÃ©ntalo de nuevo.","No se pudieron leer los archivos de parches de este juego. IntÃ©ntalo de nuevo en un momento.","Los interruptores pueden estar desactualizados.","Tema, efectos e idioma de este panel.","Este archivo de cheats no tiene cheats","IntÃ©ntalo de nuevo abajo","ActÃ­valo en Ajustes","Zona de peligro","Ir a lo Ãºltimo","Eliminar el archivo en uso","Fuente de cheats","Filtrar juegos","Ver tus juegos","CrÃ©ditos","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"fa-IR":["ÛÚ© ÚÛØª Ø¯ÙØ¹Ù ÙØ¨Ù Ø¨Ø§Ø¹Ø« Ú©Ø±Ø´ Ø¨Ø§Ø²Û Ø´Ø¯.","Ø¯Ø±Ø¨Ø§Ø±Ù","Ø¹ÙÙÛØ§Øª","Ø¹ÙÙÛØ§ØªâÙØ§","Ø§ÙØ²ÙØ¯Ù ÙØ§ÛÙ","Ø§ÙØ²ÙØ¯Ù ÚÛØª","Ø§ÙØ²ÙØ¯Ù Ø¨Ù Ø¹ÙØ§ÙÙâÙÙØ¯ÛâÙØ§","ÛÚ© Ú¯Ø²ÛÙÙ â CheatRunner Ø¨Ù ÙÙÙÛ Ú¯Ø²ÛÙÙâÙØ§Û ÙØ± Ø¨Ø§Ø²Û Ø§Ø¶Ø§ÙÙ ÙÛâÚ©ÙØ¯ Ù ÛÚ© ØªØ±Ú©ÛØ¨ Ø¯Ú©ÙÙ Ø¨Ø±Ø§Û Ø¨Ø§Ø² Ú©Ø±Ø¯Ù ØªØ±ÛÙØ± Ø¨Ø§Ø²Û Ø¯Ø± Ø­Ø§Ù Ø§Ø¬Ø±Ø§ ÙØ±Ø§ÙÙ ÙÛâÚ©ÙØ¯.","Ù¾ÛØ´Ø±ÙØªÙ","ÙÙÙ","ØªÙØ§Ù Ú©ÙÚ©âÙØ§ ØµØ±Ù ÙØ¯Ù ÙØ§ Ø¨Ø±Ø§Û ØªÙÛÙ ÛÚ© PS5 Ø¨Ø§ ÙØ±ÛÙÙØ± Ù¾Ø§ÛÛÙ (1.xxâ4.xx) Ø¨Ø±Ø§Û ØªØ­ÙÛÙØ ØªÙØ³Ø¹Ù Ù Ø§Ø±ØªÙØ§ÙØ§Û Ø¢ÛÙØ¯Ù ÙÛâØ´ÙØ¯.","ÙÙÙ Ø¯Ø§ÙÙÙØ¯ÙØ§ ÙØ§ÙÙÙÙ Ø¨ÙØ¯ÙØ¯","ÙÙÙ Ø¨Ø§Ø²ÛâÙØ§","ÙØ§ÛÙâÙØ§Û ØªØºÛÛØ± Ú©Ø±Ø¯Ù Ø±Ø§ ÙÙ Ø¨ÙâØ±ÙØ²Ø±Ø³Ø§ÙÛ Ú©Ù","Ø§Ø¹ÙØ§Ù Ø´Ø¯","Ø§Ø¹ÙØ§Ù","Ø¨Ø±ÙØ§ÙÙâÙØ§","Ø§ÙØªØ®Ø§Ø¨ Ø®ÙØ¯Ú©Ø§Ø±","Ø¨Ø§Ø²Ú¯Ø´Øª","ÙØ±ÙØ± Ú©ØªØ§Ø¨Ø®Ø§ÙÙ","Ø¯Ú©ÙÙâÙØ§","Buy Me a Coffee","ÚÛØª","ÙÙØªÙØ± ÚÛØª","CheatRunner - ØªØ±ÛÙØ± ÚÛØª Ø¨Ø±Ø§Û PS5 Ø´ÙØ§!","ÚÛØªâÙØ§","ÚÛØªâÙØ§ Ù¾Ø³ Ø§Ø² Ø§Ø¬Ø±Ø§Û Ø¨Ø§Ø²Û Ø¨Ù Ø§ÛÙ ÙØ¯Øª ÙÚ¯Ù Ø¯Ø§Ø´ØªÙ ÙÛâØ´ÙÙØ¯.","Ø§Ø¨ØªØ¯Ø§ Ø¨Ø§Ø²ÛâÙØ§Û Ø¯Ø§Ø±Ø§Û ÚÛØª","ÛÚ© Ø´Ø¨Ú©Ù Ø§ÙØªØ®Ø§Ø¨ Ú©ÙÛØ¯","Circle","Ù¾Ø§Ú© Ú©Ø±Ø¯Ù ÙÛÙØªØ±ÙØ§","Ù¾Ø§Ú© Ú©Ø±Ø¯Ù Ø¹ÙØ§ÙØª","Ù¾Ø§Ú© Ú©Ø±Ø¯Ù Ø¬Ø³ØªØ¬Ù","Ø¨Ø³ØªÙ","Ø¬ÙØ¹ Ú©Ø±Ø¯Ù","Ø¬ÙØ¹ Ú©Ø±Ø¯Ù ÙÙÙ","Ø·Ø±Ø­ Ø±ÙÚ¯ Ø§ÛÙ Ø¯Ø§Ø´Ø¨ÙØ±Ø¯.","Ø§ÙØ¬ÙÙ","ÙØ®Ø§Ø²Ù ÚÛØª Ø¬Ø§ÙØ¹Ù","ÙÛØ§ÙØ¨Ø± Ø¯Ø³ØªÙ Ù ÙÙÙÛ Ø¨Ø§Ø²Û","Ú©Ù¾Û","Ú©Ù¾Û ÙØ´Ø§ÙÛ","Ú©Ù¾Û ÙÙÙ","Ú©Ù¾Û Ù¾ÙØ´Ù ÚÛØªâÙØ§","Ú©Ù¾Û Ù¾ÛÙÙØ¯","ØªØ´Ø®ÛØµ Ú©Ø±Ø´","Ø¯ÙØ¹Ù ÙØ¨Ù Ø¨Ø§Ø²Û Ø±Ø§ Ú©Ø±Ø´ Ú©Ø±Ø¯","Cross","Ø§Ø±Ø² Ø¯ÛØ¬ÛØªØ§Ù","Ø­ÙØ§ÛØª Ø¨Ø§ Ø§Ø±Ø² Ø¯ÛØ¬ÛØªØ§Ù","ØªÛØ±Ù","Ø­Ø°Ù","ØºÛØ±ÙØ¹Ø§Ù Ú©Ø±Ø¯Ù ÙÙÙ","Ù¾Ø§ÛÛÙ","Ø¯Ø§ÙÙÙØ¯ ÙÙÙ ÙÙØ§Ø¨Ø¹","Ø¯Ø§ÙÙÙØ¯ ÚÛØªâÙØ§Û Ø¬Ø§ÙØ¹Ù","Ø¯Ø§ÙÙÙØ¯ ÙØ§ÙÙÙÙ Ø¨ÙØ¯","ÙØ§ÛÙâÙØ§Û ÚÛØª Ø¢ÙØ§Ø¯Ù Ø±Ø§ Ø¨Ø±Ø§Û Ø¨Ø§Ø²ÛâÙØ§ÛØªØ§Ù Ø¯Ø§ÙÙÙØ¯ Ú©ÙÛØ¯. Ø¯Ø± /data/cheatrunner/cheats Ø°Ø®ÛØ±Ù ÙÛâØ´ÙÙØ¯ Ù Ø¯Ø§ÙÙÙØ¯ÙØ§ Ø¯Ø± Ù¾Ø³âØ²ÙÛÙÙ Ø§Ø¯Ø§ÙÙ ÙÛâÛØ§Ø¨ÙØ¯.","Ø¯Ø§ÙÙÙØ¯ Ø§ÙØªØ®Ø§Ø¨âØ´Ø¯Ù","Ø¯Ø§ÙÙÙØ¯ Ø´Ø¯Ù","ÙØ§ÛÙ .mc4Ø .shn ÛØ§ .json Ø±Ø§ Ø§ÛÙØ¬Ø§ Ø±ÙØ§ Ú©ÙÛØ¯ ÛØ§ Ø¨Ø±Ø§Û Ø§ÙØªØ®Ø§Ø¨ Ú©ÙÛÚ© Ú©ÙÛØ¯","ÙØ¹Ø§ÙâØ³Ø§Ø²Û ÙÛØ§ÙØ¨Ø± Ø¯Ø³ØªÙ","ÙØ¹Ø§ÙâØ³Ø§Ø²Û ÙÛØ§ÙØ¨Ø±","ÙÙØªÙØ± Ø±ÙØ´Ù","Enter Ø¨Ø±Ø§Û Ø¨Ø§Ø² Ú©Ø±Ø¯Ù","ÙÙØ· Ø®Ø·Ø§ÙØ§","Ø¯ÙÛÙ","ÙØ§ÙÙÙÙ","Ø¹ÙØ§ÙÙâÙÙØ¯ÛâÙØ§","ÙØ§ÛÙâÙØ§","ÙÛÙØªØ±","ÙÛÙØªØ± Ú¯Ø²Ø§Ø±Ø´âÙØ§","Ø§Ú¯Ø± Ø¨Ø§Ø²Û Ø¨ÙØ§ÙØ§ØµÙÙ Ù¾Ø³ Ø§Ø² Ø±ÙØ´Ù Ú©Ø±Ø¯Ù ÚÛØª Ø¨Ø³ØªÙ Ø´ÙØ¯Ø Ø¢Ù ÚÛØª Ø¹ÙØ§ÙØªâÚ¯Ø°Ø§Ø±Û Ø´ÙØ¯.","Ø¨Ø±Ø§Û Ú©Ø§Ø±Ø¨Ø±Ø§Ù. ØªÙØ³Ø· Ú©Ø§Ø±Ø¨Ø±Ø§Ù.","Ø§Ø¹ÙØ§Ù Ø§Ø¬Ø¨Ø§Ø±Û","Ø¨Ø§Ø²ÛâÙØ§","Ø¨Ø§Ø²ÛâÙØ§Û Ø¯Ø§Ø±Ø§Û ÚÛØª","Ø¨Ø§Ø²ÛâÙØ§Û Ø¯Ø§Ø±Ø§Û Ù¾Ú","ÙØ§ØµÙÙ Ø¨ÛÙ ÚÛØªâÙØ§ (ÙÛÙÛâØ«Ø§ÙÛÙ)","Ø¹ÙÙÙÛ","Ø¨Ø±Ø§Û ÙØ¹Ø§Ù Ø´Ø¯Ù ÙÛØ§ÙØ¨Ø± Ø¨Ø§ ÙÙ ÙÚ¯Ù Ø¯Ø§Ø´ØªÙ ÙÛâØ´ÙÙØ¯.","ÙØ¯Øª ÙÚ¯ÙâØ¯Ø§Ø´ØªÙ (ÙÛÙÛâØ«Ø§ÙÛÙ)","Ø¯Ù Ø¯Ú©ÙÙ Ø±Ø§ Ø¨Ø§ ÙÙ ÙÚ¯Ù Ø¯Ø§Ø±ÛØ¯ ØªØ§ ØªØ±ÛÙØ± Ø¨Ø§Ø²Û Ø¯Ø± Ø­Ø§Ù Ø§Ø¬Ø±Ø§ Ø¨Ø§Ø² Ø´ÙØ¯.","Ú©Ø§Ø´Û ØµÙØ­Ù Ø§ØµÙÛ","Ø¨Ù Ø±Ø§Ø¨Ø· Ú©Ø§Ø±Ø¨Ø±Û Ø³ÛØ³ØªÙ ÙØªØµÙ ÙÛâØ´ÙØ¯. Ø¨ÙâØ·ÙØ± Ù¾ÛØ´âÙØ±Ø¶ Ø®Ø§ÙÙØ´ Ø§Ø³Øª - Ø¯Ø± Ø¨Ø±Ø®Û ÙØ±ÛÙÙØ±ÙØ§ ÙÙÚ©Ù Ø§Ø³Øª ØµÙØ­Ù Ø§ØµÙÛ Ú©Ø±Ø´ Ú©ÙØ¯.","ÙØ¯Øª ÙÚ¯Ù Ø¯Ø§Ø´ØªÙ ØªØ±Ú©ÛØ¨ ØªØ§ ÙØ¹Ø§Ù Ø´Ø¯Ù.","ÙÛØ²Ø§Ù Ø¬Ø²Ø¦ÛØ§Øª Ø«Ø¨ØªâØ´Ø¯Ù Ø¯Ø± Ú¯Ø²Ø§Ø±Ø´.","Ø§Ú¯Ø± CheatRunner Ø¨Ø±Ø§ÛØªØ§Ù ÙÙÛØ¯ Ø¨ÙØ¯Ø Ø§Ø² ØªÙØ³Ø¹Ù Ù¾Ø±ÙÚÙ Ø­ÙØ§ÛØª Ú©ÙÛØ¯! ð«¶","Ø¯Ø± Ø­Ø§Ù Ø§Ø³ØªÙØ§Ø¯Ù","ÙÙÚ¯Ø§Ù Ø±Ø§ÙâØ§ÙØ¯Ø§Ø²ÛØ Ú©Ø§Ø´Û CheatRunner Ø±Ø§ Ø±ÙÛ ØµÙØ­Ù Ø§ØµÙÛ PS5 ÙØµØ¨ ÙÛâÚ©ÙØ¯.","Ù¾ÛÙØ³ØªÙ Ø¨Ù Ø¯ÛØ³Ú©ÙØ±Ø¯","Ko-FiØ ÙÙÙÙØ Ø§Ø±Ø² Ø¯ÛØ¬ÛØªØ§ÙØ PIX","Ø²Ø¨Ø§Ù","Ø²Ø¨Ø§Ù Ø§ÛÙ Ø¯Ø§Ø´Ø¨ÙØ±Ø¯.","Ø¢Ø®Ø±ÛÙ Ø¨Ø§Ø²Û","ÚÙ¾","Ø±ÙØ´Ù","ÙØ¶Ø¹ÛØª ÙÙØªÙØ± ÚÛØª Ù ÙØµÙÙ Ø­Ø§ÙØ¸Ù.","ÙØ§ÛÙâÙØ§Û ÚÛØª ÙØ­ÙÛ","Ø³Ø·Ø­ Ú¯Ø²Ø§Ø±Ø´","ÙÛÙØªØ± Ø³Ø·Ø­ Ú¯Ø²Ø§Ø±Ø´","Ú¯Ø²Ø§Ø±Ø´âÙØ§","Ø§ØµÙÛ","Ú©ÙÛØ¯ Ø§ØµÙÛ Ø§Ø¹ÙØ§Ù ÚÛØªâÙØ§ Ù Ù¾ÚâÙØ§.","Ø§ØµÙØ§Ø­ ÙØ³ØªØ± Ú©Ø¯","ÙÙÚ©Ù Ø§Ø³Øª Ø¨Ø§ Ø§ÛÙ ÙØ³Ø®Ù Ø¨Ø§Ø²Û Ø³Ø§Ø²Ú¯Ø§Ø± ÙØ¨Ø§Ø´Ø¯","ÙØµÙÙ Ø­Ø§ÙØ¸Ù","ÙÛÙÙâØ´Ø¨","Ø­Ø¯Ø§ÙÙ Ø²ÙØ§Ù Ø¨ÛÙ Ø¯Ù ØªØºÛÛØ± ÚÛØª.","ÙØ§Ù Ø§ÙÙ-Û","ÙØ§Ù Û-Ø§ÙÙ","ÙÙÙØ² ÙØ§ÛÙ ÚÛØªÛ Ø¨Ø±Ø§Û Ø§ÛÙ Ø¨Ø§Ø²Û ÙØ¬ÙØ¯ ÙØ¯Ø§Ø±Ø¯.","ÙÙÙØ² Ø¹ÙØ§ÙÙâÙÙØ¯ÛâØ§Û ÙØ¯Ø§Ø±ÛØ¯. Ø±ÙÛ Ø³ØªØ§Ø±Ù ÛÚ© Ø¨Ø§Ø²Û Ø¨Ø²ÙÛØ¯ ØªØ§ Ø§Ø¶Ø§ÙÙ Ø´ÙØ¯.","Ø¯Ø± Ø­Ø§Ù Ø§Ø¬Ø±Ø§ ÙÛØ³Øª","Ø±ÙØ´Ù","Ø¨Ø§Ø² Ú©Ø±Ø¯Ù Ø±ÙÛ Ú¯ÙØ´Û","Ø¨Ø§Ø² Ú©Ø±Ø¯Ù Ø±ÙÛ Ú¯ÙØ´Û (Ú©Ø¯ QR)","Ø¯Ø§Ø´Ø¨ÙØ±Ø¯ Ø±Ø§ Ø±ÙÛ Ú¯ÙØ´Û Ø¨Ø§Ø² Ú©ÙÛØ¯","Ø¨Ø§Ø² Ú©Ø±Ø¯Ù ØªØ±ÛÙØ±","Ú¯Ø²ÛÙÙâÙØ§","ÙØ³Ø®Ù Ø¯ÛÚ¯Ø±","ØµÙØ­Ù","Ù¾ÚâÙØ§","Ø¨Ø§Ø²Û","Ú©Ø¯ QR","Ú©Ø¯ QR ÙØ´Ø§ÙÛ Ø¯Ø§Ø´Ø¨ÙØ±Ø¯","ÙØ± Ø¨Ø§Ø± Ú©Ù Ø§ÛÙ Ø¨Ø§Ø²Û Ø§Ø¬Ø±Ø§ ÙÛâØ´ÙØ¯ ÚÛØªâÙØ§Û Ø±ÙØ´Ù Ø¯ÙØ¨Ø§Ø±Ù Ø§Ø¹ÙØ§Ù Ø´ÙÙØ¯","Ø¢ÙØ§Ø¯Ù","Ø§Ø®ÛØ±","Ø¨Ø§Ø²Ø¢ÙØ±Û","Ø¨Ø§Ø²Ø¢ÙØ±Û ÙÙØ±Ø³Øª Ø¨Ø§Ø²ÛâÙØ§","Ø§Ø¬Ø±Ø§Û ÙØ¬Ø¯Ø¯","Ú¯Ø²Ø§Ø±Ø´ ÙØ´Ú©Ù","Ø§Ø³Ú©Ù Ø¯ÙØ¨Ø§Ø±Ù Ø¨Ø§Ø²ÛâÙØ§Û ÙØµØ¨âØ´Ø¯Ù","Ø¨Ø§Ø²ÙØ´Ø§ÙÛ Ø¨Ù Ù¾ÛØ´âÙØ±Ø¶","ÚÛØªâÙØ§Û ÙØ§Ø¨Ø³ØªÙ Ø¨Ù ÙØ³ØªØ± Ú©Ø¯ Ø¨ÙâØ·ÙØ± Ø®ÙØ¯Ú©Ø§Ø± Ø­Ù Ø´ÙÙØ¯.","Ø¨Ø§Ø²Ú¯Ø±Ø¯Ø§ÙÛ","Ø±Ø§Ø³Øª","Ø¯Ø± Ø­Ø§Ù Ø§Ø¬Ø±Ø§","ÙÙâØ§Ú©ÙÙÙ Ø¯Ø± Ø­Ø§Ù Ø§Ø¬Ø±Ø§","Ø°Ø®ÛØ±Ù Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø±","Ø¨Ø§ Ø¯ÙØ±Ø¨ÛÙ Ú¯ÙØ´Û Ø§Ø³Ú©Ù Ú©ÙÛØ¯. Ú¯ÙØ´Û Ø¨Ø§ÛØ¯ Ø¨Ù ÙÙØ§Ù Wi-Fi Ù¾ÙÛâØ§Ø³ØªÛØ´Ù Ûµ ÙØªØµÙ Ø¨Ø§Ø´Ø¯.","Ø¬Ø³ØªØ¬Ù","Ø¬Ø³ØªØ¬Ù Ø¨Ø§ ÙØ§Ù ÛØ§ Ø´ÙØ§Ø³Ù Ø¹ÙÙØ§Ù","Ø¬Ø³ØªØ¬ÙÛ Ø¨Ø§Ø²ÛâÙØ§","Ø¬Ø³ØªØ¬ÙÛ Ø¨Ø§Ø²ÛâÙØ§Ø ØµÙØ­ÙâÙØ§ Ù Ø¹ÙÙÛØ§ØªâÙØ§","ÛÚ© ÙØ´Ø§ÙÛ Ø±Ø§ Ø§ÙØªØ®Ø§Ø¨ Ú©ÙÛØ¯ ØªØ§ Ú©Ø§ÙÙ Ú©Ù¾Û Ø´ÙØ¯.","ØªÙØ¸ÛÙØ§Øª","ÙÙØ§ÛØ´ Ú©Ø¯ QR","Ø®Ø§ÙÙØ´ Ú©Ø±Ø¯Ù payload","ÙØ±ØªØ¨âØ³Ø§Ø²Û Ø¨Ø§Ø²ÛâÙØ§","ÙÙØ¨Ø¹","ÙÙØ§Ø¨Ø¹","Ø³Ø±Ø¹Øª","Square","Ø¨Ø§Ø²Û Ø±Ø§ Ø§Ø¬Ø±Ø§ Ú©ÙÛØ¯","Ø¨Ø±Ø§Û Ø§Ø¹ÙØ§Ù Ù¾ÚâÙØ§ Ø§ÛÙ Ø¨Ø§Ø²Û Ø±Ø§ Ø§Ø¬Ø±Ø§ Ú©ÙÛØ¯.","ÙØ¶Ø¹ÛØª","ÙØ¶Ø¹ÛØª Ù Ú¯Ø²Ø§Ø±Ø´âÙØ§","Ø­ÙØ§ÛØª Ø§Ø² CheatRunner","Ø­ÙØ§ÛØª Ø§Ø² Ø¨Ø±Ø²ÛÙ","Ø§Ø² Ø§ÛÙ Ù¾Ø±ÙÚÙ Ø­ÙØ§ÛØª Ú©ÙÛØ¯","Ø­Ø§ÙÛØ§Ù","Ø³ÛØ³ØªÙ","ÙÙØ§Ø±Ø¯ Ø¶Ø±ÙØ±Û - Ù¾ÙØ³ØªÙ Ù Ú©ÙÛØ¯ ÙÙØªÙØ± ÚÛØª.","Ù¾ÙØ³ØªÙ","Ù¾ÙØ³ØªÙ Ù ÙÙØªÙØ± ÚÛØª","Ø²ÙØ§Ù Ø¨Ø§ÙÛâÙØ§ÙØ¯Ù","Ø²ÙØ§Ù Ø¨Ø§Ø²Û","Ø´ÙØ§Ø³Ù Ø¹ÙÙØ§Ù","Ú©ÙÛØ¯ÙØ§","TouchPad","ØªØ±ÛÙØ±","Triangle","Ø¨Ø§ÙØ§","Ø¨ÙâØ±ÙØ²","Ø§Ø³ØªÙØ§Ø¯Ù","ÙØ³Ø®Ù","Ø§ÙØªØ¸Ø§Ø± Ù¾Ø³ Ø§Ø² Ø´Ø±ÙØ¹ Ø¨Ø§Ø²Û (ÙÛÙÛâØ«Ø§ÙÛÙ)","ÙØ´Ø¯Ø§Ø±ÙØ§ Ù Ø®Ø·Ø§ÙØ§","Ú©Ø§Ø±ÙØ§ÛÛ Ú©Ù CheatRunner Ø§ÙØ¬Ø§Ù Ø¯Ø§Ø¯Ù - ÙÙÚ¯Ø§Ù Ø¨Ø±ÙØ² ÙØ´Ú©Ù ÙÙÛØ¯ Ø§Ø³Øª.","Ø¯Ø§Ø±Ø§Û ÚÛØª","ââ Ù¾ÛÙØ§ÛØ´","ÙÙÙ ØªÙØ¸ÛÙØ§Øª Ø¨Ù Ù¾ÛØ´âÙØ±Ø¶ Ø¨Ø±ÙÛâÚ¯Ø±Ø¯Ø¯. Ù¾ÙØ±Øª ÙØ¨ Ø­ÙØ¸ ÙÛâØ´ÙØ¯.","ÚÛØª Ø¯ÛÚ¯Ø±Û Ø¯Ø± Ø­Ø§Ù Ø§Ø¹ÙØ§Ù Ø§Ø³Øª. Ú©ÙÛ Ø¨Ø¹Ø¯ Ø¯ÙØ¨Ø§Ø±Ù ØªÙØ§Ø´ Ú©ÙÛØ¯.","ÙØ± ÙØ³Ø®Ù","Ø¨Ù ÙØ± Ø­Ø§Ù Ø§Ø¹ÙØ§Ù Ú©Ù","Ø§ÙØªØ®Ø§Ø¨ Ø®ÙØ¯Ú©Ø§Ø± Ø¨Ø§Ø²Ú¯Ø±Ø¯Ø§ÙÛ Ø´Ø¯.","Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± Ù¾Ø§Ú© Ø´Ø¯.","Ø°Ø®ÛØ±Ù Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± ({n}). ÙÙÚ¯Ø§Ù Ø§Ø¬Ø±Ø§Û Ø¨Ø§Ø²Û Ø±ÙØ´Ù ÙÛâØ´ÙÙØ¯.","Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± Ø®Ø§ÙÛ Ø°Ø®ÛØ±Ù Ø´Ø¯.","ÚÛØª Ø´ÙØ§Ø±Ù {n}","ÙØ§ÛÙ ÚÛØª Ø§ÙØ²ÙØ¯Ù Ø´Ø¯.","ÙØ§ÛÙ ÚÛØª Ø­Ø°Ù Ø´Ø¯.","ÙØ§ÛÙ ÚÛØª Ø§ÙØªØ®Ø§Ø¨ Ø´Ø¯.","CheatRunner ÙÙÚ¯Ø§Ù Ø§Ø¬Ø±Ø§ Ù¾Ø§Ø³Ø® ÙØ¯Ø§Ø¯.","Ù¾ÙØ´Ù ÚÛØªâÙØ§ Ú©Ù¾Û Ø´Ø¯.","Ø¯Ø± Ø­Ø§Ù Ø¨Ø±Ø±Ø³Û {name}","Ù¾Ø§Ú© Ú©Ø±Ø¯Ù Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± ({n})","ÙÙØ§ÛØ³Ù Ù Ø¯Ø§ÙÙÙØ¯ ÙØ§ÛÙâÙØ§ ÛÚ©ÛâÛÚ©Û","Ø¨Ø§ ÚÛØª Ø¯ÛÚ¯Ø±Û ØªØ¯Ø§Ø®Ù Ø¯Ø§Ø±Ø¯","Ú©Ù¾Û ÙØ´Ø¯. ÙØªÙ Ø±Ø§ Ø¯Ø³ØªÛ Ø§ÙØªØ®Ø§Ø¨ Ú©ÙÛØ¯.","Ù¾Ø§Ú© Ú©Ø±Ø¯Ù Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± ÙÙÚ©Ù ÙØ´Ø¯.","Ú©Ù¾Û ØªØ´Ø®ÛØµ ÙÙÚ©Ù ÙØ´Ø¯.","ØºÛØ±ÙØ¹Ø§Ù Ú©Ø±Ø¯Ù ÚÛØªâÙØ§ ÙÙÚ©Ù ÙØ´Ø¯.","Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ù¾ÚâÙØ§ ÙÙÚ©Ù ÙØ´Ø¯.","Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û ÙÙØ±Ø³Øª Ø¨Ø§Ø²ÛâÙØ§ ÙÙÚ©Ù ÙØ´Ø¯.","Ø°Ø®ÛØ±Ù Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± ÙÙÚ©Ù ÙØ´Ø¯.","Ø°Ø®ÛØ±Ù ÙØ´Ø¯. Ø¯ÙØ¨Ø§Ø±Ù ØªÙØ§Ø´ Ú©ÙÛØ¯.","Ø§ÙØªØ®Ø§Ø¨ ÙØ§ÛÙ ÙÙÚ©Ù ÙØ´Ø¯.","Ø´Ø±ÙØ¹ Ø¯Ø§ÙÙÙØ¯ ÙÙÚ©Ù ÙØ´Ø¯.","Ø´Ø±ÙØ¹ ÙÙÚ©Ù ÙØ´Ø¯.","ØªØºÛÛØ± ÙØ¶Ø¹ÛØª ÚÛØª ÙÙÚ©Ù ÙØ´Ø¯.","Ø­Ø°Ù ÙØ§ÙÙÙÙ Ø¨ÙØ¯.","Ø­Ø°Ù ÙØ§ÛÙ","{file} Ø­Ø°Ù Ø´ÙØ¯Ø","ØªØ´Ø®ÛØµ Ú©Ù¾Û Ø´Ø¯. Ø¢Ù Ø±Ø§ Ø¯Ø± Ú¯Ø²Ø§Ø±Ø´ ÙØ´Ú©Ù Ø¨ÚØ³Ø¨Ø§ÙÛØ¯.","Ø¯Ø± Ø­Ø§Ù Ø¯Ø§ÙÙÙØ¯ {name}","ÙÙØªÙØ± Ø®Ø§ÙÙØ´","ÙÙÙâÚÛØ² Ø¨ÙâØ±ÙØ² Ø§Ø³Øª","Ø¨Ø§Ø² Ú©Ø±Ø¯Ù ÙÙÙ","Ø¯Ø±ÛØ§ÙØª Ú©Ù ÙØ®Ø²Ù Ø¨ÙâØµÙØ±Øª ÛÚ©Ø¬Ø§","ÙØ§ÛÙ v{a} / Ø¨Ø§Ø²Û v{b}","{name} ØªÙØ§Ù Ø´Ø¯","{name} Ø¨ÙâØ§Ø¬Ø¨Ø§Ø± Ø§Ø¹ÙØ§Ù Ø´ÙØ¯Ø","Ø¨Ø§Ø²Û Ø§Ø¬Ø±Ø§ Ø´Ø¯.","Ø¨Ø±Ø§Û ÙØ³Ø®Ù Ø¯ÛÚ¯Ø±Û Ø§Ø² Ø¨Ø§Ø²Û Ø§Ø³Øª Ù ÙÙÚ©Ù Ø§Ø³Øª Ø¨Ø§Ø²Û Ø±Ø§ Ú©Ø±Ø´ Ú©ÙØ¯. Ø¨Ø§Ø²Ú¯Ø±Ø¯Ø§ÙÛ Ø¨Ø§ÛØªâÙØ§Û Ø§ØµÙÛ Ø±Ø§ Ø¨Ø±ÙÛâÚ¯Ø±Ø¯Ø§ÙØ¯.","Ø§Ø¬Ø±Ø§ ÙØ§ÙÙÙÙ Ø¨ÙØ¯.","Ø§Ø¬Ø±Ø§ ÙØ§ÙÙÙÙ Ø¨ÙØ¯: {msg}","Ø¯Ø±Ø®ÙØ§Ø³Øª Ø§Ø¬Ø±Ø§ ÙØ§ÙÙÙÙ Ø¨ÙØ¯. Ø´Ø§ÛØ¯ Ø¨Ø§Ø²Û ÙÙÙØ² Ø¯Ø± Ø­Ø§Ù Ø´Ø±ÙØ¹ Ø§Ø³Øª.","Ø²ÙØ§Ù Ø§Ø¬Ø±Ø§ ØªÙØ§Ù Ø´Ø¯. Ø¨Ø±Ø±Ø³Û Ú©ÙÛØ¯ Ø¨Ø§Ø²Û Ø§Ø¬Ø±Ø§ Ø´Ø¯Ù ÛØ§ ÙÙ.","Ø¯Ø± Ø­Ø§Ù Ø§Ø¬Ø±Ø§Û {name}...","Ú¯Ø²Ø§Ø±Ø´âÙØ§ Ú©Ù¾Û Ø´Ø¯.","Ø³Ø§Ø®ØªÙâØ´Ø¯Ù Ø¨Ø§ {heart} ØªÙØ³Ø· maj0r","Ø¨Ø§ v{v} ÙØ·Ø§Ø¨ÙØª Ø¯Ø§Ø±Ø¯","ÙÛØ§Ø²ÙÙØ¯ v{v}","Ø¨Ø¯ÙÙ Ø¨Ø§Ø²Û","Ø¨Ø§Ø²ÛâØ§Û Ø¨Ø§ Ø¬Ø³ØªØ¬ÙÛ Ø´ÙØ§ ÙØ·Ø§Ø¨ÙØª ÙØ¯Ø§Ø±Ø¯.","ÙÙÙØ² Ø¨Ø§Ø²Û Ø¯Ø§Ø±Ø§Û ÚÛØªÛ ÙÛØ³Øª. Ø§Ø² Ø¨Ø®Ø´ ÙÙØ§Ø¨Ø¹ Ø¯Ø±ÛØ§ÙØª Ú©ÙÛØ¯.","ÙÙÙØ² Ú¯Ø²Ø§Ø±Ø´Û ÙÛØ³Øª.","Ø®Ø·Û ÙØ·Ø§Ø¨ÙØª ÙØ¯Ø§Ø±Ø¯.","Ø¢ÙØ§Ø¯Ù ÙÛØ³Øª","ÙÙØ±Ø¯ Ø§Ø®ÛØ±Û ÙÛØ³Øª. Ø¨Ø§Ø²Û Ø±Ø§ Ø§Ø¬Ø±Ø§ Ú©ÙÛØ¯ ÛØ§ ØªØ±ÛÙØ± Ø¢Ù Ø±Ø§ Ø¨Ø§Ø² Ú©ÙÛØ¯.","ÚÛØ²Û Ø¨Ø±Ø§Û Ú©Ù¾Û ÙÛØ³Øª.","ÚÛØ²Û Ø±ÙØ´Ù ÙØ¨ÙØ¯.","Ø¨Ø§Ø² Ú©Ø±Ø¯Ù ØªØ±ÛÙØ±: {name}","Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± Ù¾Ú Ù¾Ø§Ú© Ø´Ø¯.","Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± Ù¾Ú Ø°Ø®ÛØ±Ù Ø´Ø¯.","Ú©Ø¯ QR Ø¯Ø± Ø¯Ø³ØªØ±Ø³ ÙÛØ³Øª.","Ø®ÙØ§ÙØ¯Ù ÙÙØ±Ø³Øª ÙØ§ÛÙâÙØ§ Ø§Ø² GitHub","Ø­Ø°Ù Ø§Ø² Ø¹ÙØ§ÙÙâÙÙØ¯ÛâÙØ§","Ø¨Ø§Ø²ÙØ´Ø§ÙÛ ÙØ§ÙÙÙÙ Ø¨ÙØ¯.","ØªÙØ¸ÛÙØ§Øª Ø¨Ø§Ø²ÙØ´Ø§ÙÛ Ø´ÙØ¯Ø","Ø°Ø®ÛØ±Ù Ø´Ø¯","Ø°Ø®ÛØ±Ù ÙØ§ÛÙâÙØ§Û ÚÛØª","Ø¯Ø± Ø­Ø§Ù Ø°Ø®ÛØ±Ù...","Ù¾Ø³ Ø§Ø² Ø¢Ù ÛÚ© CheatRunner.elf Ø¬Ø¯ÛØ¯ Ø§Ø² Ø±Ø§ÛØ§ÙÙ Ø¨ÙØ±Ø³ØªÛØ¯.","ØªÙØ¸ÛÙØ§Øª Ø¨Ø§Ø²ÙØ´Ø§ÙÛ Ø´Ø¯.","Ø®Ø§ÙÙØ´ Ú©Ø±Ø¯Ù","CheatRunner Ø®Ø§ÙÙØ´ Ø´ÙØ¯Ø","ÙÙØ¨Ø¹ {i} Ø§Ø² {n}","Ø§Ø¨ØªØ¯Ø§ Ø¨Ø§Ø²Û Ø±Ø§ Ø§Ø¬Ø±Ø§ Ú©ÙÛØ¯.","Ø¨Ø§Ø²Û ÙÙÙØ² Ø¯Ø± Ø­Ø§Ù Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø§Ø³Øª.","Ø¨Ø§Ø²Û ÙÙÙØ² Ø¯Ø± Ø­Ø§Ù Ø´Ø±ÙØ¹ Ø§Ø³Øª. ÚÙØ¯ Ø«Ø§ÙÛÙ Ø¨Ø¹Ø¯ Ø¯ÙØ¨Ø§Ø±Ù ØªÙØ§Ø´ Ú©ÙÛØ¯.","ÙÙØ±Ø³Øª Ø¨Ø§Ø²ÛâÙØ§ ÙØ¹ÙØ§Ù Ø¯Ø± Ø¯Ø³ØªØ±Ø³ ÙÛØ³Øª.","ÙØ¹ÙØ§Ù ÙÙÛâØªÙØ§Ù Ø§ÛÙ ÚÛØª Ø±Ø§ ØªØºÛÛØ± Ø¯Ø§Ø¯.","Ø§ÛÙ ÚÛØª ÙØ¨ÙØ§Ù Ø¨Ø§Ø²Û Ø±Ø§ Ú©Ø±Ø´ Ú©Ø±Ø¯Ù Ø§Ø³Øª. Ø¨Ø±Ø§Û ØªÙØ§Ø´ Ø¯ÙØ¨Ø§Ø±Ù Ø¹ÙØ§ÙØª Ø±Ø§ Ù¾Ø§Ú© Ú©ÙÛØ¯.","Ø§ÛÙ ÙØ§ÛÙ Ø¨Ø±Ø§Û ÙØ³Ø®ÙâØ§Û Ø§Ø² Ø¨Ø§Ø²Û Ø§Ø³Øª Ú©Ù Ø¨Ø§ ÙØ³Ø®Ù ÙØµØ¨âØ´Ø¯Ù ÙØ±Ù Ø¯Ø§Ø±Ø¯.","Ø§ÛÙ Ú©Ø§Ø± ÙØ§ÛÙ Ø±Ø§ Ø§Ø² Ú©ÙØ³ÙÙ Ø­Ø°Ù ÙÛâÚ©ÙØ¯. ÙÛâØªÙØ§ÙÛØ¯ Ø¯ÙØ¨Ø§Ø±Ù Ø§Ø² Ø¨Ø®Ø´ ÙÙØ§Ø¨Ø¹ Ø¯Ø§ÙÙÙØ¯Ø´ Ú©ÙÛØ¯.","ÙØ¯Øª: {t}","ÙØ´Ø§ÙÛ Ú©Ù¾Û Ø´Ø¯.","Ø¯Ø± Ø­Ø§Ù Ø§Ø³ØªØ®Ø±Ø§Ø¬ {name}","Ù¾Ú Ù¾Ø´ØªÛØ¨Ø§ÙÛâÙØ´Ø¯Ù","Ø¢Ù¾ÙÙØ¯ ÙØ§ÙÙÙÙ Ø¨ÙØ¯.","Ø§Ø² ÙØ§ÛÙ .mc4Ø .shn ÛØ§ .json Ø§Ø³ØªÙØ§Ø¯Ù Ú©ÙÛØ¯.","Ø§Ø² ÙØ³Ø®Ù Ø¯ÛÚ¯Ø± Ø§Ø³ØªÙØ§Ø¯Ù Ø´ÙØ¯Ø","Ø¨Ù ÙØ± Ø­Ø§Ù Ø§Ø³ØªÙØ§Ø¯Ù Ú©Ù","Ø¯Ø± Ø§ÙØªØ¸Ø§Ø± Ø¨Ø§Ø²Ú¯Ø´Øª CheatRunner...","ÙØ´Ø§ÙÛ {label} Ú©Ù¾Û Ø´Ø¯","ÙÙÙ ÙÙØ§Ø¨Ø¹","ÙÛØ§ÙÚ¯ÛÙ {s}","ÙÙâØ§Ú©ÙÙÙ","ÙØ§ÛÙ ÚÛØª","ÙØ·Ø§Ø¨Ù v{v}","{n} ÚÛØª Ø®Ø§ÙÙØ´ Ø´Ø¯.","{n} ÙØ§ÛÙ Ø¬Ø¯ÛØ¯ ÛØ§ Ø¨ÙâØ±ÙØ²Ø´Ø¯Ù Ø°Ø®ÛØ±Ù Ø´Ø¯","{n} Ø¹ÙÙØ§Ù","{n} Ø¹ÙÙØ§Ù","{n} Ø±ÙØ² Ù¾ÛØ´","{n} Ø³Ø§Ø¹Øª Ù¾ÛØ´","{n} Ø¯ÙÛÙÙ Ù¾ÛØ´","{page} - CheatRunner","{a} Ø§Ø² {n} Ø±ÙØ´Ù","{h} Ø³Ø§Ø¹Øª {m} Ø¯ÙÛÙÙ","{m} Ø¯ÙÛÙÙ","{s} Ø«Ø§ÙÛÙ","{m} Ø¯ÙÛÙÙ {s} Ø«Ø§ÙÛÙ","ØªÙØ³Ø· {a}","Ø¨Ù CheatRunner Ø®ÙØ´ Ø¢ÙØ¯ÛØ¯.","ÙÙÙØ² ÙØ§ÛÙ ÚÛØªÛ ÙÛØ³Øª. Ø§Ø² Ø¨Ø®Ø´ ÙÙØ§Ø¨Ø¹ ÚÛØªâÙØ§Û Ø¬Ø§ÙØ¹Ù Ø±Ø§ Ø¨Ú¯ÛØ±ÛØ¯ ÛØ§ Ø¨Ø§Ø²Û Ø±Ø§ Ø¨Ø§Ø² Ú©ÙÛØ¯ Ù ÙØ§ÛÙÛ Ø±ÙØ§ Ú©ÙÛØ¯.","ÙØºÙ","Ø§Ø¯Ø§ÙÙ","Ø¯Ø± Ø­Ø§Ù Ø§ØªØµØ§Ù ÙØ¬Ø¯Ø¯...","Ú©Ù¾Û Ø´Ø¯","Ú©Ù¾Û Ø´Ø¯.","ÙØ§ÙØ´Ø®Øµ","Ù¾ÙØ¬Ø±Ù Ú¯ÙØªÚ¯Ù","Ø®Ø§ÙÙØ´","Ø¯Ø³ØªØ±Ø³ÛâÙØ§","Ø³Ø§ÙÙ","ÙÙØ¬ÙØ¯ ÙÛØ³Øª","ÙØ§ÙÙÙÙ Ø¨ÙØ¯.","Ø¯Ø± Ø­Ø§Ù Ø§Ø¬Ø±Ø§","ØªÙØ§Ø´ Ø¯ÙØ¨Ø§Ø±Ù","Ø¨Ø§Ø²Û","Ø¬Ø³ØªØ¬ÙÛ ÚÛØªâÙØ§","maj0r Ø±Ø§ Ø¯Ø± X Ø¯ÙØ¨Ø§Ù Ú©ÙÛØ¯","maj0r Ø±Ø§ Ø¯Ø± Bluesky Ø¯ÙØ¨Ø§Ù Ú©ÙÛØ¯","Ø³Ø§Ø®ØªÙ maj0r - ð§ð·","Ø¨Ù Ù¾Ø±ÙÚÙ Ø³ØªØ§Ø±Ù Ø¨Ø¯ÙÛØ¯! â­","Ø§Ø±ØºÙØ§ÙÛ","Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø±","Ø¨Ø§Ø²ÙØ´Ø§ÙÛ","Ø¨Ø±Ø§Û Ø§ÛÙ Ø¨Ø§Ø²Û Ù¾ÚÛ ÙØ¬ÙØ¯ ÙØ¯Ø§Ø±Ø¯.","Ù¾ÚâÙØ§Û XML Ø®ÙØ¯ Ø±Ø§ Ø¯Ø± /data/cheatrunner/patches/xml ÙØ±Ø§Ø± Ø¯ÙÛØ¯","Ø¨ÙâØ±ÙØ²Ø±Ø³Ø§ÙÛ ÙÙØ¬ÙØ¯ Ø§Ø³Øª","ÙØ³Ø®Ù {v} Ø¯Ø± Ø¯Ø³ØªØ±Ø³ Ø§Ø³Øª.","ÙØ±ÙØ± ÚÛØªâÙØ§ Ø¨ÙâØµÙØ±Øª Ø¢ÙÙØ§ÛÙ","ÙÙÙ Ø¨Ø§Ø²ÛâÙØ§Û Ø¯Ø§Ø±Ø§Û ÚÛØª Ø±Ø§ Ø¬Ø³ØªâÙØ¬Ù Ú©ÙÛØ¯ Ù Ø¨Ø¨ÛÙÛØ¯ Ø¨Ø§Ø²Û Ø´ÙØ§ Ù¾ÙØ´Ø´ Ø¯Ø§Ø¯Ù Ø´Ø¯Ù Ø§Ø³Øª ÛØ§ ÙÙ.","Ø¨Ø§Ø² Ú©Ø±Ø¯Ù Ø³Ø§ÛØª","ÙØ®Ø§Ø²Ù Ù¾Ú Ø¬Ø§ÙØ¹Ù","Ù¾ÚâÙØ§Û Ø¢ÙØ§Ø¯Ù Ø¨Ø±Ø§Û Ø¨Ø§Ø²ÛâÙØ§Û Ø´ÙØ§. ÚÛØ²Û Ø¯Ø§ÙÙÙØ¯ ÙÙÛâØ´ÙØ¯ Ù ÙØ®Ø²Ù Ø¯Ø± ØªØ¨ Ø¬Ø¯ÛØ¯ Ø¨Ø§Ø² ÙÛâØ´ÙØ¯.","Ø¨Ø§Ø² Ú©Ø±Ø¯Ù PS-Game-Patch","Ø¬ÙÙÙâÙØ§Û Ø¨ØµØ±Û","Ø­Ø§ÙØª Ú©Ø§ÙØ´âÛØ§ÙØªÙ ÙØ­Ù Ø´Ø¯ÙØ Ø³Ø§ÛÙâÙØ§ Ù Ø§ÙÛÙÛØ´ÙâÙØ§ Ø±Ø§ Ø®Ø§ÙÙØ´ ÙÛâÚ©ÙØ¯. Ø¯Ø± ÙØ±ÙØ±Ú¯Ø± PS5 Ù¾ÛØ´âÙØ±Ø¶ Ø§Ø³Øª.","Ú©Ø§ÙÙ","Ú©Ø§ÙØ´âÛØ§ÙØªÙ","ÙÙØ§ÛØ´ Ø¨ÛØ´ØªØ± ({n})","Ù¾ÛØ´ÙÙØ§Ø¯Û","ÙØ³Ø®Ù Ø±Ø³ÙÛ PS-Game-Patch. ÙØµÙÙâÙØ§ Ø¯Ø± /data/cheatrunner/patches Ø°Ø®ÛØ±Ù ÙÛâØ´ÙÙØ¯.","Ø¯Ø§ÙÙÙØ¯ ÙØµÙÙâÙØ§","Ø°Ø®ÛØ±Ù ÙØ§ÛÙâÙØ§Û ÙØµÙÙ","Ø¨Ø±Ø®Û ÙØµÙÙâÙØ§ ÙÙØ· ÙÙØªÛ Ú©Ø§Ø± ÙÛâÚ©ÙÙØ¯ Ú©Ù ÙÙÚ¯Ø§Ù Ø´Ø±ÙØ¹ Ø¨Ø§Ø²Û Ø§Ø¹ÙØ§Ù Ø´ÙÙØ¯: Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± Ø±Ø§ Ø°Ø®ÛØ±Ù Ú©ÙÛØ¯ Ù Ø¨Ø§Ø²Û Ø±Ø§ Ø¯ÙØ¨Ø§Ø±Ù Ø±Ø§ÙâØ§ÙØ¯Ø§Ø²Û Ú©ÙÛØ¯.","Ø¹ÛØ¨âÛØ§Ø¨Û","ÙØ´Ú©ÙÛ Ø±Ø§ Ú¯Ø²Ø§Ø±Ø´ Ú©ÙÛØ¯ ÛØ§ ÙÙÙ ØªÙØ¸ÛÙØ§Øª Ø±Ø§ Ø¨Ù Ø­Ø§ÙØª Ù¾ÛØ´âÙØ±Ø¶ Ø¨Ø±Ú¯Ø±Ø¯Ø§ÙÛØ¯.","Ø§Ø·ÙØ§Ø¹Ø§Øª ØªØ´Ø®ÛØµÛ Ø±Ø§ Ú©Ù¾Û ÙÛâÚ©ÙØ¯ Ù ØµÙØ­Ù Ú¯Ø²Ø§Ø±Ø´ ÙØ´Ú©Ù Ø¯Ø± GitHub Ø±Ø§ Ø¨Ø§Ø² ÙÛâÚ©ÙØ¯.","ÚÛØªâÙØ§Ø ÙØµÙÙâÙØ§ Ù Ù¾Ø±ÙÙØ§ÛÙâÙØ§Û Ø¨Ø§Ø±Ú¯Ø°Ø§Ø±Û Ø®ÙØ¯Ú©Ø§Ø± ØªØºÛÛØ±Û ÙÙÛâÚ©ÙÙØ¯.","ÙÙÙâÚÛØ² Ø®ÙØ¨ Ø¨Ù ÙØ¸Ø± ÙÛâØ±Ø³Ø¯.","ÙÙØ±Ø¯ ÙÛØ§Ø²ÙÙØ¯ ØªÙØ¬Ù Ø§Ø³Øª - Ø¬Ø²Ø¦ÛØ§Øª Ù¾Ø§ÛÛÙ Ø±Ø§ Ø¨Ø±Ø±Ø³Û Ú©ÙÛØ¯.","ÛÚ© ÙØ§ÛÙ .mc4Ø .shn ÛØ§ .json Ø§Ø² Ø±Ø§ÛØ§ÙÙÙ Ø´ÙØ§.","Ø§Ø² Ø¨Ø®Ø´ ÙØ§ÛÙâÙØ§ ÛØ§ ÙÙØ§Ø¨Ø¹ Ø§Ø¶Ø§ÙÙ Ú©ÙÛØ¯","Ø¸Ø§ÙØ±","Ø¸Ø§ÙØ±Ø ÙÙØªÙØ± ÚÛØª Ù Ú¯Ø²ÛÙÙâÙØ§Û Ø¯Ø³ØªÙ. ØªØºÛÛØ±Ø§Øª Ø¨ÙØ§ÙØ§ØµÙÙ Ø°Ø®ÛØ±Ù ÙÛâØ´ÙÙØ¯.","ÙØ§ÛÙ ÚÛØª","ÙØ§ÛÙâÙØ§Û ÚÛØª Ø§ÛÙ Ø¨Ø§Ø²Û","CheatRunner Ø¨Ø§Ø²ÛâÙØ§Û ÙØµØ¨âØ´Ø¯Ù Ø±ÙÛ Ø§ÛÙ Ú©ÙØ³ÙÙ Ø±Ø§ ÙÙØ±Ø³Øª ÙÛâÚ©ÙØ¯. ÛÚ© Ø¨Ø§Ø²Û ÙØµØ¨ Ú©ÙÛØ¯ Ù ÙÙØ±Ø³Øª Ø±Ø§ Ø¨Ø§Ø²Ø¢ÙØ±Û Ú©ÙÛØ¯.","ØªØ§ Ø²ÙØ§ÙÛ Ú©Ù Ø±ÙØ´Ù ÙØ´ÙØ¯Ø ÚÛØªâÙØ§ Ù Ù¾ÚâÙØ§ Ø§Ø¹ÙØ§Ù ÙÙÛâØ´ÙÙØ¯.","Ø¯Ø± Ø­Ø§Ù Ø¨Ø±Ø±Ø³Û...","Ø§ÙØªØ®Ø§Ø¨ ÙØ§ÛÙ Ø¯ÛÚ¯Ø±","Ø¨Ø§Ø±Ú¯ÛØ±Û ÙØ´Ø¯","ÙØ§ÛÙ ÚÛØª Ø¨Ø§Ø±Ú¯ÛØ±Û ÙØ´Ø¯","Ù¾ÚâÙØ§ Ø¨Ø§Ø±Ú¯ÛØ±Û ÙØ´Ø¯ÙØ¯","ØªÙØ¸ÛÙØ§Øª Ø¨Ø§Ø±Ú¯ÛØ±Û ÙØ´Ø¯ÙØ¯","Ø¨Ø§Ø²ÛâÙØ§Û Ø´ÙØ§ Ø¨Ø§Ø±Ú¯ÛØ±Û ÙØ´Ø¯ÙØ¯","ÙØ¶Ø¹ÛØª ÙØ¹ÙÛ ÚÛØªâÙØ§ Ø®ÙØ§ÙØ¯Ù ÙØ´Ø¯.","ÚÛØªâÙØ§Û Ø§ÙØ¬ÙÙ Ø±Ø§ Ø§Ø² Ø¨Ø®Ø´ ÙÙØ§Ø¨Ø¹ Ø¯Ø§ÙÙÙØ¯ Ú©ÙÛØ¯ ÛØ§ ÙØ§ÛÙÛ Ø§Ø² Ø±Ø§ÛØ§ÙÙâØªØ§Ù Ø§Ø¶Ø§ÙÙ Ú©ÙÛØ¯.","ÛÚ©Û Ø±Ø§ Ø§Ø² ÙÙØ§Ø¨Ø¹ Ø¯Ø§ÙÙÙØ¯ Ú©ÙÛØ¯ ÛØ§ ÙØ§ÛÙÛ Ø±Ø§ Ø¯Ø± Ù¾Ø§ÛÛÙ Ø±ÙØ§ Ú©ÙÛØ¯.","ÚÛØªâÙØ§ Ù Ù¾ÚâÙØ§Û Ø¢ÙØ§Ø¯Ù Ø¨Ø±Ø§Û Ø¨Ø§Ø²ÛâÙØ§ÛØªØ§Ù Ø±Ø§ Ø¯Ø§ÙÙÙØ¯ Ú©ÙÛØ¯. Ø¯Ø§ÙÙÙØ¯ÙØ§ ÙÙÚ¯Ø§Ù Ú¯Ø´ØªâÙÚ¯Ø°Ø§Ø± Ø§Ø¯Ø§ÙÙ ÙÛâÛØ§Ø¨ÙØ¯.","ÙØ¬ÙÙØ¹ÙÙ Ø±Ø³ÙÛ Ù¾ÚâÙØ§ Ø±Ø§ Ø§Ø² ÙÙØ§Ø¨Ø¹ Ø¯Ø§ÙÙÙØ¯ Ú©ÙÛØ¯ ÛØ§ Ù¾ÚâÙØ§Û XML Ø®ÙØ¯ Ø±Ø§ Ø¯Ø± /data/cheatrunner/patches/xml Ø¨Ú¯Ø°Ø§Ø±ÛØ¯.","Ø¯Ø± Ø­Ø§Ù Ø¯Ø§ÙÙÙØ¯","Ø¯Ø± Ø­Ø§Ù Ø¯Ø§ÙÙÙØ¯ {p}%","Ø¯Ø§ÙÙÙØ¯ÙØ§","ÙÙØªÙØ±","Ø¯Ø± Ø­Ø§Ù Ø¯ÙØ¨Ø§Ù Ú©Ø±Ø¯Ù Ø¢Ø®Ø±ÛÙ Ø®Ø·âÙØ§","Ø¯Ø±ÛØ§ÙØª ÚÛØª","ÙØ¶Ø¹ÛØª","Ú©ØªØ§Ø¨Ø®Ø§ÙÙ","ÙØ¶Ø¹ÛØª Ø²ÙØ¯ÙÙ ÙÙØªÙØ± ÚÛØª Ù Ù¾Ú Ø­Ø§ÙØ¸Ù Ù Ú©Ø§Ø±ÙØ§ÛÛ Ú©Ù CheatRunner Ø§ÙØ¬Ø§Ù Ø¯Ø§Ø¯Ù Ø§Ø³Øª.","Ø§Ø®Ø¨Ø§Ø±Ø Ø±Ø§ÙÙÙØ§ÛÛ Ù Ø§ÙØ±Ø§Ø¯Û Ú©Ù Ù¾Ø´Øª CheatRunner ÙØ³ØªÙØ¯.","ÙÙÙØ² ÙØ§ÛÙ ÚÛØªÛ Ø¨Ø±Ø§Û Ø§ÛÙ Ø¨Ø§Ø²Û ÙÛØ³Øª","ÚÛØªÛ ÙØ·Ø§Ø¨Ù Â«{q}Â» ÛØ§ÙØª ÙØ´Ø¯.","ÙÙÙØ² ÙØ§ÛÙÛ ÙÛØ³Øª","ÙÙÙØ² Ø¨Ø§Ø²ÛâØ§Û Ù¾ÛØ¯Ø§ ÙØ´Ø¯","ÙØ§ÛÙ ÙØ­ÙÛ Ù¾ÛØ¯Ø§ ÙØ´Ø¯","Ù¾ÚÛ Ø¨Ø±Ø§Û Ø§ÛÙ Ø¨Ø§Ø²Û ÙÛØ³Øª","Ø¯Ø± Ø­Ø§Ù Ø§Ø¬Ø±Ø§","Ø¨Ø§Ø² Ú©Ø±Ø¯Ù ØªÙØ¸ÛÙØ§Øª","ÙÙÚ¯Ø§Ù ÙØ·Ø§ÙØ¹Ù ÙØªÙÙÙ Ø§Ø³Øª","ØªØ±Ø¬ÛØ­Ø§Øª","Ø­Ø§ÙØª Â«Ú©Ø§ÙØ´âÛØ§ÙØªÙÂ» ÙÙÙÙ Ø§ÙÛÙÛØ´ÙâÙØ§ Ø±Ø§ Ø­Ø°Ù ÙÛâÚ©ÙØ¯. Ø§ÛÙ Ø­Ø§ÙØª Ø¯Ø± ÙØ±ÙØ±Ú¯Ø± PS5 Ù¾ÛØ´âÙØ±Ø¶ Ø§Ø³Øª.","Ø¯Ø± /data/cheatrunner/cheats Ø°Ø®ÛØ±Ù ÙÛâØ´ÙÙØ¯. ÙØ§ÛÙÛ Ú©Ù Â«Ø¯Ø± Ø­Ø§Ù Ø§Ø³ØªÙØ§Ø¯ÙÂ» Ø¹ÙØ§ÙØª Ø®ÙØ±Ø¯Ù ÙÙØ§Ù Ø§Ø³Øª Ú©Ù ØªØ±ÛÙØ± ÙÛâØ®ÙØ§ÙØ¯.","ÙÙØªÛ ÚÛØ²Û Ú©Ø§Ø± ÙÙÛâÚ©ÙØ¯ Ú¯Ø²Ø§Ø±Ø´ Ø¨ÙØ±Ø³ØªÛØ¯.","Ø¨Ø±Ø§Û Ø§Ø³ØªÙØ§Ø¯Ù Ø§Ø² ÚÛØªâÙØ§ Ø§Ø¬Ø±Ø§ÛØ´ Ú©ÙÛØ¯","Ø¨Ø±Ø§Û Ø§Ø³ØªÙØ§Ø¯Ù Ø§Ø² ÚÛØªâÙØ§Û Ø§ÛÙ Ø¨Ø§Ø²ÛØ Ø¢Ù Ø±Ø§ Ø§Ø¬Ø±Ø§ Ú©ÙÛØ¯.","Ø¬Ø²Ø¦ÛØ§Øª ÙÙÛ","ÙÙØªÙØ± ÚÛØª Ø®Ø§ÙÙØ´ Ø§Ø³Øª.","ÙÙØ§Ø¯ÛØ± ÙØ¹ÙÛ Ø®ÙØ§ÙØ¯Ù ÙØ´Ø¯ÙØ¯Ø Ù¾Ø³ Ø¨ÙâØ¬Ø§Û ÙÙØ§ÛØ´ ÙÙØ§Ø¯ÛØ± ÙØ§Ø¯Ø±Ø³Øª ÚÛØ²Û ÙØ´Ø§Ù Ø¯Ø§Ø¯Ù ÙÙÛâØ´ÙØ¯.","ÙÙØ§Ø±Ø¯ Ø§ØµÙÛ Ø¨Ø±Ø§Û Ø§Ø¹ÙØ§Ù ÚÛØªâÙØ§ Ù Ù¾ÚâÙØ§.","ÙØ§ÛÙ ÙØ¬ÙØ¯ Ø¯Ø§Ø±Ø¯ Ø§ÙØ§ Ø®ÙØ§ÙØ¯Ù ÙØ´Ø¯. ÙÙÚ©Ù Ø§Ø³Øª Ø¢Ø³ÛØ¨ Ø¯ÛØ¯Ù Ø¨Ø§Ø´Ø¯ ÛØ§ ÙÙÙØ² Ø¯Ø± Ø­Ø§Ù Ú©Ù¾Û Ø¨Ø§Ø´Ø¯.","ÙØ§ÛÙ Ø¨Ø§Ø±Ú¯ÛØ±Û Ø´Ø¯ Ø§ÙØ§ Ø®Ø§ÙÛ Ø§Ø³Øª. ÙØ§ÛÙ Ø¯ÛÚ¯Ø±Û Ø¨Ø±Ø§Û Ø§ÛÙ Ø¨Ø§Ø²Û Ø§ÙØªØ­Ø§Ù Ú©ÙÛØ¯.","ÙÙØ±Ø³Øª Ø¨Ø§Ø²ÛâÙØ§ Ø§Ú©ÙÙÙ Ø¯Ø± Ø¯Ø³ØªØ±Ø³ ÙÛØ³Øª. ÙØ·ÙØ¦Ù Ø´ÙÛØ¯ Ú©ÙØ³ÙÙ Ø±ÙØ´Ù Ø§Ø³Øª Ù Ø¯ÙØ¨Ø§Ø±Ù ØªÙØ§Ø´ Ú©ÙÛØ¯.","ÙØ§ÛÙâÙØ§Û Ù¾Ú Ø§ÛÙ Ø¨Ø§Ø²Û Ø®ÙØ§ÙØ¯Ù ÙØ´Ø¯ÙØ¯. Ú©ÙÛ Ø¨Ø¹Ø¯ Ø¯ÙØ¨Ø§Ø±Ù ØªÙØ§Ø´ Ú©ÙÛØ¯.","ÙÙÚ©Ù Ø§Ø³Øª ÙØ¶Ø¹ÛØª Ú©ÙÛØ¯ÙØ§ ÙØ¯ÛÙÛ Ø¨Ø§Ø´Ø¯.","ØªÙØ Ø¬ÙÙÙâÙØ§ Ù Ø²Ø¨Ø§Ù Ø§ÛÙ Ù¾ÙÙ.","Ø§ÛÙ ÙØ§ÛÙ ÚÛØª ÙÛÚ ÚÛØªÛ ÙØ¯Ø§Ø±Ø¯","Ù¾Ø§ÛÛÙ Ø¯ÙØ¨Ø§Ø±Ù ØªÙØ§Ø´ Ú©ÙÛØ¯","Ø§Ø² ØªÙØ¸ÛÙØ§Øª Ø±ÙØ´ÙØ´ Ú©ÙÛØ¯","ÙØ§Ø­ÛÙÙ Ø®Ø·Ø±","Ø±ÙØªÙ Ø¨Ù Ø¢Ø®Ø±ÛÙ","Ø­Ø°Ù ÙØ§ÛÙ Ø¯Ø± Ø­Ø§Ù Ø§Ø³ØªÙØ§Ø¯Ù","ÙÙØ¨Ø¹ ÚÛØª","ÙÛÙØªØ± Ø¨Ø§Ø²ÛâÙØ§","Ø¯ÛØ¯Ù Ø¨Ø§Ø²ÛâÙØ§Û Ø´ÙØ§","Ø³Ø§Ø²ÙØ¯Ú¯Ø§Ù","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"fr-FR":["Un cheat a fait planter le jeu la derniÃ¨re fois.","Ã propos","Action","Actions","Ajouter un fichier","Ajouter des cheats","Ajouter aux favoris","Ajoute une entrÃ©e â CheatRunner au menu d'options de chaque jeu et une combinaison de boutons qui ouvre le trainer du jeu en cours.","AvancÃ©","Tous","Toutes les contributions serviront Ã  acquÃ©rir une PS5 Ã  firmware ancien (1.xxâ4.xx) pour la recherche, le dÃ©veloppement et les Ã©volutions futures.","Tous les tÃ©lÃ©chargements ont Ã©chouÃ©","Tous les jeux","Mettre aussi Ã  jour les fichiers modifiÃ©s","AppliquÃ©","Appliquer","Applis","SÃ©lection auto","Retour","Parcourez votre bibliothÃ¨que","Boutons","Buy Me a Coffee","Cheat","Moteur de cheats","CheatRunner - Cheat Trainer pour votre PS5 !","Cheats","Les cheats sont retenus ce dÃ©lai aprÃ¨s le lancement d'un jeu.","Avec cheats d'abord","Choisissez un rÃ©seau","Circle","Effacer les filtres","Effacer le signalement","Effacer la recherche","Fermer","RÃ©duire","RÃ©duire le menu","Palette de couleurs de ce tableau de bord.","CommunautÃ©","DÃ©pÃ´ts de cheats communautaires","Raccourci manette et menu du jeu","Copier","Copier l'URL","Tout copier","Copier le dossier des cheats","Copier le lien","DÃ©tection des plantages","A fait planter le jeu la derniÃ¨re fois","Cross","Crypto","Soutien en crypto","Sombre","Supprimer","Tout dÃ©sactiver","Bas","Tout tÃ©lÃ©charger","TÃ©lÃ©chargez des cheats communautaires","Ãchec du tÃ©lÃ©chargement","TÃ©lÃ©chargez des fichiers de cheats prÃªts Ã  l'emploi pour vos jeux. Ils sont enregistrÃ©s dans /data/cheatrunner/cheats et les tÃ©lÃ©chargements continuent en arriÃ¨re-plan.","TÃ©lÃ©charger la sÃ©lection","TÃ©lÃ©chargÃ©","DÃ©posez un fichier .mc4, .shn ou .json ici, ou cliquez pour parcourir","Activer le raccourci manette","Activer le raccourci","Moteur actif","EntrÃ©e pour ouvrir","Erreurs uniquement","Exacte","Ãchec","Favoris","Fichiers","Filtrer","Filtrer les journaux","Signaler un cheat si le jeu se ferme juste aprÃ¨s son activation.","Pour les utilisateurs. Par les utilisateurs.","Forcer l'application","Jeux","Jeux avec cheats","Jeux avec patchs","DÃ©lai entre cheats (ms)","GÃ©nÃ©ral","Maintenus ensemble pour dÃ©clencher le raccourci.","DurÃ©e de maintien (ms)","Maintenez deux boutons ensemble pour ouvrir le trainer du jeu en cours.","Tuile de l'Ã©cran d'accueil","S'accroche Ã  l'interface systÃ¨me. DÃ©sactivÃ© par dÃ©faut - sur certains firmwares, cela peut faire planter l'Ã©cran d'accueil.","DurÃ©e de maintien de la combinaison avant dÃ©clenchement.","Niveau de dÃ©tail Ã©crit dans le journal.","Si CheatRunner vous a Ã©tÃ© utile, pensez Ã  soutenir le dÃ©veloppement du projet ! ð«¶","UtilisÃ©","Installe la tuile CheatRunner sur l'Ã©cran d'accueil de la PS5 au dÃ©marrage.","Rejoindre le Discord","Ko-Fi, cafÃ©, crypto, PIX","Langue","Langue de ce tableau de bord.","DerniÃ¨re partie","Gauche","Clair","Ãtat du moteur de cheats et de l'Ã©criture en mÃ©moire.","Fichiers de cheats locaux","Niveau de journal","Filtre de niveau de journal","Journaux","Principal","Interrupteur gÃ©nÃ©ral pour appliquer cheats et patchs.","Correction du master code","Peut ne pas correspondre Ã  cette version du jeu","Patch mÃ©moire","Minuit","DÃ©lai minimal entre deux activations de cheat.","Nom A-Z","Nom Z-A","Pas encore de fichier de cheats pour ce jeu.","Aucun favori pour l'instant. Touchez l'Ã©toile d'un jeu pour l'ajouter.","Non lancÃ©","ActivÃ©","Ouvrir sur le tÃ©lÃ©phone","Ouvrir sur le tÃ©lÃ©phone (code QR)","Ouvrez le tableau de bord sur votre tÃ©lÃ©phone","Ouvrir le trainer","Options","Autre version","Page","Patchs","Jouer","Code QR","Code QR de l'adresse du tableau de bord","RÃ©appliquer les cheats activÃ©s Ã  chaque lancement de ce jeu","PrÃªt","RÃ©cents","Actualiser","Actualiser la liste des jeux","Relancer","Signaler un problÃ¨me","RÃ©analyser les jeux installÃ©s","RÃ©tablir les valeurs par dÃ©faut","RÃ©soudre automatiquement les cheats dÃ©pendant d'un master code.","Restaurer","Droite","En cours","En cours maintenant","Enregistrer l'autoload","Scannez-le avec l'appareil photo de votre tÃ©lÃ©phone. Le tÃ©lÃ©phone doit Ãªtre sur le mÃªme Wi-Fi que la PS5.","Rechercher","Rechercher par nom ou ID de titre","Rechercher des jeux","Rechercher jeux, pages et actions","SÃ©lectionnez une adresse pour la copier en entier.","ParamÃ¨tres","Afficher le code QR","ArrÃªter le payload","Trier les jeux","Source","Sources","Vitesse","Square","Lancez le jeu","Lancez ce jeu pour appliquer des patchs.","Ãtat","Ãtat et journaux","Soutenez CheatRunner","Soutien depuis le BrÃ©sil","Soutenez ce projet","Soutiens","SystÃ¨me","L'essentiel : thÃ¨me et interrupteur du moteur de cheats.","ThÃ¨me","ThÃ¨me et moteur de cheats","Temps restant","Temps de jeu","ID du titre","Interrupteurs","TouchPad","Trainer","Triangle","Haut","Ã jour","Utiliser","Version","Attente aprÃ¨s le lancement du jeu (ms)","Avertissements et erreurs","Ce que CheatRunner a fait - utile quand quelque chose ne marche pas.","Avec cheats","ââ naviguer","Tous les paramÃ¨tres reviennent Ã  leurs valeurs par dÃ©faut. Le port web est conservÃ©.","Un autre cheat est en cours d'application. RÃ©essayez dans un instant.","Toute version","Appliquer quand mÃªme","SÃ©lection automatique rÃ©tablie.","Autoload supprimÃ©.","Autoload enregistrÃ© ({n}). Ils s'activent au lancement du jeu.","Autoload enregistrÃ© vide.","Cheat nÂ° {n}","Fichier de cheats ajoutÃ©.","Fichier de cheats supprimÃ©.","Fichier de cheats sÃ©lectionnÃ©.","CheatRunner a cessÃ© de rÃ©pondre pendant le lancement.","Dossier des cheats copiÃ©.","VÃ©rification de {name}","Effacer l'autoload ({n})","Comparaison et tÃ©lÃ©chargement des fichiers un par un","Entre en conflit avec un autre cheat","Ãchec de la copie. SÃ©lectionnez le texte manuellement.","Impossible d'effacer l'autoload.","Impossible de copier le diagnostic.","Impossible de dÃ©sactiver les cheats.","Impossible de charger les patchs.","Impossible de charger la liste des jeux.","Impossible d'enregistrer l'autoload.","Enregistrement impossible. RÃ©essayez.","Impossible de sÃ©lectionner le fichier.","Impossible de dÃ©marrer le tÃ©lÃ©chargement.","Impossible de dÃ©marrer.","Impossible de basculer le cheat.","Ãchec de la suppression.","Supprimer le fichier","Supprimer {file} ?","Diagnostic copiÃ©. Collez-le dans le signalement.","TÃ©lÃ©chargement de {name}","Moteur Ã©teint","Tout est dÃ©jÃ  Ã  jour","DÃ©velopper le menu","RÃ©cupÃ©ration de tout le dÃ©pÃ´t en une fois","Fichier v{a} / jeu v{b}","{name} terminÃ©","Forcer l'application de {name} ?","Jeu lancÃ©.","Il cible une autre version du jeu et peut faire planter le jeu. Restaurer remet les octets d'origine.","Ãchec du lancement.","Ãchec du lancement : {msg}","La demande de lancement a Ã©chouÃ©. Le jeu est peut-Ãªtre encore en cours de dÃ©marrage.","DÃ©lai de lancement dÃ©passÃ©. VÃ©rifiez si le jeu a dÃ©marrÃ©.","Lancement de {name}...","Journaux copiÃ©s.","Fait avec {heart} par maj0r","Correspond Ã  v{v}","NÃ©cessite v{v}","Aucun jeu","Aucun jeu ne correspond Ã  votre recherche.","Aucun jeu avec cheats pour l'instant. RÃ©cupÃ©rez-en dans Sources.","Pas encore de journaux.","Aucune ligne correspondante.","Pas prÃªt","Rien de rÃ©cent. Lancez un jeu ou ouvrez son trainer.","Rien Ã  copier.","Rien n'Ã©tait activÃ©.","Ouvrir le trainer : {name}","Autoload des patchs supprimÃ©.","Autoload des patchs enregistrÃ©.","Le code QR n'est pas disponible.","Lecture de la liste des fichiers depuis GitHub","Retirer des favoris","Ãchec de la rÃ©initialisation.","RÃ©initialiser les paramÃ¨tres ?","EnregistrÃ©","Enregistrement des fichiers de cheats","Enregistrement...","Envoyez ensuite un nouveau CheatRunner.elf depuis votre PC.","ParamÃ¨tres rÃ©initialisÃ©s.","ArrÃªter","ArrÃªter CheatRunner ?","Source {i} sur {n}","Lancez d'abord le jeu.","Le jeu est encore en chargement.","Le jeu dÃ©marre encore. RÃ©essayez dans quelques secondes.","La liste des jeux est indisponible pour le moment.","Ce cheat ne peut pas Ãªtre basculÃ© pour le moment.","Ce cheat a dÃ©jÃ  fait planter le jeu. Effacez le signalement pour rÃ©essayer.","Ce fichier correspond Ã  une autre version du jeu que celle installÃ©e.","Cela supprime le fichier de la console. Vous pourrez le retÃ©lÃ©charger depuis Sources.","DurÃ©e : {t}","URL copiÃ©e.","DÃ©compression de {name}","Patch non pris en charge","Ãchec de l'envoi.","Utilisez un fichier .mc4, .shn ou .json.","Utiliser une autre version ?","Utiliser quand mÃªme","En attente du retour de CheatRunner...","Adresse {label} copiÃ©e","toutes les sources","moy. {s}","Ã  l'instant","le fichier de cheats","v{v} correspond","{n} cheat(s) dÃ©sactivÃ©(s).","{n} fichier(s) nouveau(x) ou mis Ã  jour enregistrÃ©(s)","{n} titre","{n} titres","il y a {n} j","il y a {n} h","il y a {n} min","{page} - CheatRunner","{a} sur {n} actifs","{h} h {m} min","{m} min","{s} s","{m} min {s} s","par {a}","Bienvenue dans CheatRunner.","Pas encore de fichiers de cheats. RÃ©cupÃ©rez des cheats communautaires dans Sources, ou ouvrez un jeu et dÃ©posez un fichier.","Annuler","Continuer","Reconnexion...","CopiÃ©","CopiÃ©.","Inconnu","BoÃ®te de dialogue","DÃ©sactivÃ©","PrivilÃ¨ges","OK","Manquant","Ãchec.","Lancement","RÃ©essayer","Jeu","Rechercher des cheats","Suivez maj0r sur X","Suivez maj0r sur Bluesky","CrÃ©Ã© par maj0r - ð§ð·","Laissez une Ã©toile au projet ! â­","Cramoisi","Autoload","RÃ©initialiser","Aucun patch pour ce jeu.","Placez vos patchs XML dans /data/cheatrunner/patches/xml","Mise Ã  jour disponible","La version {v} est disponible.","Parcourir les cheats en ligne","Recherchez tous les jeux avec cheats et vÃ©rifiez si le vÃ´tre est couvert.","Ouvrir le site","DÃ©pÃ´ts de patchs communautaires","Des patchs prÃªts Ã  l'emploi pour vos jeux. Rien n'est tÃ©lÃ©chargÃ© : le dÃ©pÃ´t s'ouvre dans un nouvel onglet.","Ouvrir PS-Game-Patch","Effets visuels","RÃ©duit dÃ©sactive le flou, les ombres et les animations. C'est le rÃ©glage par dÃ©faut dans le navigateur de la PS5.","Complet","RÃ©duit","Afficher plus ({n})","RecommandÃ©","Version officielle de PS-Game-Patch. Les patchs sont enregistrÃ©s dans /data/cheatrunner/patches.","TÃ©lÃ©charger les patchs","Enregistrement des fichiers de patchs","Certains patchs ne fonctionnent que s'ils sont appliquÃ©s au lancement du jeu : enregistrez le chargement automatique et relancez le jeu.","DÃ©pannage","Signalez un problÃ¨me ou rÃ©tablissez tous les paramÃ¨tres par dÃ©faut.","Copie le diagnostic et ouvre la page des tickets sur GitHub.","Les cheats, les patchs et les profils de chargement automatique ne sont pas modifiÃ©s.","Tout semble en ordre.","Quelque chose demande votre attention - consultez les dÃ©tails ci-dessous.","Un fichier .mc4, .shn ou .json depuis votre ordinateur.","Ajoutez-en un dans Fichiers ou Sources","Apparence","Apparence, moteur de cheats et manette. Les changements sont enregistrÃ©s aussitÃ´t.","Fichier de cheats","Fichiers de cheats de ce jeu","CheatRunner liste les jeux installÃ©s sur cette console. Installez-en un, puis actualisez la liste.","Les cheats et patchs ne peuvent pas Ãªtre appliquÃ©s tant quâil nâest pas activÃ©.","VÃ©rification...","Choisir un autre fichier","Chargement impossible","Impossible de charger le fichier de cheats","Impossible de charger les patchs","Impossible de charger les paramÃ¨tres","Impossible de charger vos jeux","Impossible de lire lâÃ©tat actuel des cheats.","TÃ©lÃ©chargez des cheats de la communautÃ© dans Sources ou ajoutez un fichier depuis votre ordinateur.","TÃ©lÃ©chargez-en un dans Sources ou dÃ©posez un fichier ci-dessous.","TÃ©lÃ©chargez des cheats et patchs prÃªts Ã  lâemploi pour vos jeux. Les tÃ©lÃ©chargements continuent pendant que vous naviguez.","TÃ©lÃ©chargez lâensemble officiel de patchs dans Sources ou placez vos patchs XML dans /data/cheatrunner/patches/xml.","TÃ©lÃ©chargement","TÃ©lÃ©chargement {p}%","TÃ©lÃ©chargements","Moteur","Suit les derniÃ¨res lignes","Obtenir des cheats","Ãtat","BibliothÃ¨que","Ãtat en direct du moteur de cheats et de la modification mÃ©moire, et activitÃ© de CheatRunner.","ActualitÃ©s, aide et les personnes derriÃ¨re CheatRunner.","Pas encore de fichier de cheats pour ce jeu","Aucun cheat ne correspond Ã  Â« {q} Â».","Pas encore de fichier","Aucun jeu trouvÃ© pour lâinstant","Aucun fichier local trouvÃ©","Aucun patch pour ce jeu","En cours de jeu","Ouvrir les paramÃ¨tres","En pause pendant la lecture","PrÃ©fÃ©rences","Le mode rÃ©duit supprime toutes les animations. Câest le rÃ©glage par dÃ©faut dans le navigateur de la PS5.","EnregistrÃ©s dans /data/cheatrunner/cheats. Le fichier marquÃ© UtilisÃ© est celui que lit le trainer.","Envoyez un rapport quand quelque chose ne marche pas.","Lancez-le pour utiliser les cheats","Lancez ce jeu pour utiliser ses cheats.","DÃ©tails techniques","Le moteur de cheats est Ã©teint.","Les valeurs actuelles nâont pas pu Ãªtre lues ; rien nâest affichÃ© plutÃ´t que des valeurs erronÃ©es.","Lâessentiel pour appliquer cheats et patchs.","Le fichier existe, mais il nâa pas pu Ãªtre lu. Il est peut-Ãªtre endommagÃ© ou encore en cours de copie.","Le fichier a Ã©tÃ© chargÃ© mais il est vide. Essayez un autre fichier pour ce jeu.","La liste des jeux est indisponible pour lâinstant. VÃ©rifiez que la console est allumÃ©e, puis rÃ©essayez.","Les fichiers de patchs de ce jeu nâont pas pu Ãªtre lus. RÃ©essayez dans un instant.","Les interrupteurs peuvent Ãªtre obsolÃ¨tes.","ThÃ¨me, effets et langue de ce tableau de bord.","Ce fichier de cheats ne contient aucun cheat","RÃ©essayez ci-dessous","Activez-le dans ParamÃ¨tres","Zone dangereuse","Aller au plus rÃ©cent","Supprimer le fichier utilisÃ©","Source de cheats","Filtrer les jeux","Voir vos jeux","CrÃ©dits","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"it-IT":["Un cheat ha mandato in crash il gioco l'ultima volta.","Informazioni","Azione","Azioni","Aggiungi un file","Aggiungi cheat","Aggiungi ai preferiti","Aggiunge una voce â CheatRunner al menu opzioni di ogni gioco e una combinazione di pulsanti che apre il trainer del gioco in esecuzione.","Avanzate","Tutti","Tutti i contributi andranno al nostro obiettivo di acquistare una PS5 con firmware basso (1.xxâ4.xx) per ricerca, sviluppo e futuri aggiornamenti.","Tutti i download sono falliti","Tutti i giochi","Aggiorna anche i file modificati","Applicato","Applica","App","Selezione automatica","Indietro","Sfoglia la tua libreria","Pulsanti","Buy Me a Coffee","Cheat","Motore cheat","CheatRunner - Cheat Trainer per la tua PS5!","Cheat","I cheat vengono trattenuti per questo tempo dopo l'avvio di un gioco.","Prima con cheat","Scegli una rete","Circle","Cancella filtri","Rimuovi segnalazione","Cancella ricerca","Chiudi","Comprimi","Comprimi menu","Combinazione di colori di questa dashboard.","Community","Repository di cheat della community","Scorciatoia del controller e menu del gioco","Copia","Copia URL","Copia tutto","Copia cartella dei cheat","Copia link","Rilevamento crash","Ha mandato in crash il gioco l'ultima volta","Cross","Cripto","Supporto in cripto","Scuro","Elimina","Disattiva tutti","GiÃ¹","Scarica tutte le fonti","Scarica i cheat della community","Download non riuscito","Scarica file di cheat giÃ  pronti per i tuoi giochi. Vengono salvati in /data/cheatrunner/cheats e i download proseguono in background.","Scarica selezione","Scaricato","Trascina qui un file .mc4, .shn o .json, oppure clicca per sfogliare","Abilita scorciatoia del controller","Abilita scorciatoia","Motore attivo","Invio apre","Solo errori","Esatta","Falliti","Preferiti","File","Filtra","Filtra i log","Segnala un cheat se il gioco si chiude subito dopo l'attivazione.","Per gli utenti. Dagli utenti.","Forza applicazione","Giochi","Giochi con cheat","Giochi con patch","Intervallo tra i cheat (ms)","Generali","Tenuti premuti insieme per attivare la scorciatoia.","Tempo di pressione (ms)","Tieni premuti due pulsanti insieme per aprire il trainer del gioco in esecuzione.","Riquadro nella schermata Home","Si aggancia all'interfaccia di sistema. Disattivato di default: su alcuni firmware puÃ² mandare in crash la schermata Home.","Per quanto tenere premuta la combinazione prima che scatti.","Quanto dettaglio viene scritto nel log.","Se CheatRunner ti Ã¨ stato utile, considera di sostenere lo sviluppo del progetto! ð«¶","In uso","Installa il riquadro CheatRunner nella schermata Home della PS5 all'avvio.","Unisciti al Discord","Ko-Fi, caffÃ¨, cripto, PIX","Lingua","Lingua di questa dashboard.","Ultima partita","Sinistra","Chiaro","Stato del motore cheat e della scrittura in memoria.","File di cheat locali","Livello di log","Filtro livello di log","Log","Principale","Interruttore generale per applicare cheat e patch.","Correzione master code","Potrebbe non corrispondere a questa versione del gioco","Patch della memoria","Mezzanotte","Tempo minimo tra due attivazioni di cheat.","Nome A-Z","Nome Z-A","Nessun file di cheat per questo gioco.","Ancora nessun preferito. Tocca la stella di un gioco per aggiungerlo.","Non in esecuzione","Attivo","Apri sul telefono","Apri sul telefono (codice QR)","Apri la dashboard sul telefono","Apri trainer","Opzioni","Altra versione","Pagina","Patch","Gioca","Codice QR","Codice QR dell'indirizzo della dashboard","Riapplica i cheat ATTIVI a ogni avvio di questo gioco","Pronto","Recenti","Aggiorna","Aggiorna elenco giochi","Rilancia","Segnala un problema","Riscansiona i giochi installati","Ripristina predefiniti","Risolve automaticamente i cheat che dipendono da un master code.","Ripristina","Destra","In esecuzione","In esecuzione ora","Salva autoload","Inquadralo con la fotocamera del telefono. Il telefono deve essere sulla stessa rete Wi-Fi della PS5.","Cerca","Cerca per nome o ID del titolo","Cerca giochi","Cerca giochi, pagine e azioni","Seleziona un indirizzo per copiarlo per intero.","Impostazioni","Mostra codice QR","Arresta il payload","Ordina giochi","Sorgente","Fonti","VelocitÃ ","Square","Avvia il gioco","Avvia questo gioco per applicare le patch.","Stato","Stato e log","Sostieni CheatRunner","Supporto dal Brasile","Sostieni questo progetto","Sostenitori","Sistema","L'essenziale: tema e interruttore del motore cheat.","Tema","Tema e motore cheat","Tempo rimasto","Tempo di gioco","ID titolo","Interruttori","TouchPad","Trainer","Triangle","Su","Aggiornati","Usa","Versione","Attesa dopo l'avvio del gioco (ms)","Avvisi ed errori","Cosa ha fatto CheatRunner: utile quando qualcosa non funziona.","Con cheat","ââ naviga","Tutte le impostazioni tornano ai valori predefiniti. La porta web viene mantenuta.","Un altro cheat Ã¨ in corso di applicazione. Riprova tra un attimo.","Qualsiasi versione","Applica comunque","Selezione automatica ripristinata.","Autoload rimosso.","Autoload salvato ({n}). Si attivano all'avvio del gioco.","Autoload salvato vuoto.","Cheat n. {n}","File di cheat aggiunto.","File di cheat eliminato.","File di cheat selezionato.","CheatRunner ha smesso di rispondere durante l'avvio.","Cartella dei cheat copiata.","Controllo di {name}","Cancella autoload ({n})","Confronto e download dei file uno per uno","In conflitto con un altro cheat","Copia non riuscita. Seleziona il testo manualmente.","Impossibile cancellare l'autoload.","Impossibile copiare la diagnostica.","Impossibile disattivare i cheat.","Impossibile caricare le patch.","Impossibile caricare l'elenco dei giochi.","Impossibile salvare l'autoload.","Salvataggio non riuscito. Riprova.","Impossibile selezionare il file.","Impossibile avviare il download.","Impossibile avviare.","Impossibile attivare/disattivare il cheat.","Eliminazione non riuscita.","Elimina file","Eliminare {file}?","Diagnostica copiata. Incollala nella segnalazione.","Download di {name}","Motore spento","Tutto Ã¨ giÃ  aggiornato","Espandi menu","Scaricamento dell'intero repository in un colpo solo","File v{a} / gioco v{b}","{name} completato","Forzare l'applicazione di {name}?","Gioco avviato.","Ã pensato per un'altra versione del gioco e puÃ² mandarlo in crash. Ripristina rimette i byte originali.","Avvio non riuscito.","Avvio non riuscito: {msg}","Richiesta di avvio non riuscita. Il gioco potrebbe essere ancora in avvio.","Timeout di avvio. Controlla se il gioco Ã¨ partito.","Avvio di {name}...","Log copiati.","Fatto con {heart} da maj0r","Corrisponde a v{v}","Richiede v{v}","Nessun gioco","Nessun gioco corrisponde alla ricerca.","Ancora nessun gioco con cheat. Scaricane alcuni da Fonti.","Ancora nessun log.","Nessuna riga corrispondente.","Non pronto","Niente di recente. Avvia un gioco o apri il suo trainer.","Niente da copiare.","Niente era attivo.","Apri trainer: {name}","Autoload delle patch rimosso.","Autoload delle patch salvato.","Codice QR non disponibile.","Lettura dell'elenco file da GitHub","Rimuovi dai preferiti","Ripristino non riuscito.","Ripristinare le impostazioni?","Salvato","Salvataggio dei file di cheat","Salvataggio...","Poi invia un nuovo CheatRunner.elf dal tuo PC.","Impostazioni ripristinate.","Arresta","Arrestare CheatRunner?","Fonte {i} di {n}","Avvia prima il gioco.","Il gioco Ã¨ ancora in caricamento.","Il gioco si sta ancora avviando. Riprova tra qualche secondo.","L'elenco dei giochi non Ã¨ al momento disponibile.","Questo cheat non puÃ² essere commutato ora.","Questo cheat ha giÃ  mandato in crash il gioco. Rimuovi la segnalazione per riprovare.","Questo file Ã¨ per una versione del gioco diversa da quella installata.","Questo rimuove il file dalla console. Puoi scaricarlo di nuovo da Fonti.","Durata: {t}","URL copiato.","Estrazione di {name}","Patch non supportata","Caricamento non riuscito.","Usa un file .mc4, .shn o .json.","Usare un'altra versione?","Usa comunque","In attesa che CheatRunner torni...","Indirizzo {label} copiato","tutte le fonti","media {s}","poco fa","il file di cheat","v{v} corrisponde","{n} cheat disattivato/i.","{n} file nuovo/i o aggiornato/i salvato/i","{n} titolo","{n} titoli","{n} g fa","{n} h fa","{n} min fa","{page} - CheatRunner","{a} su {n} attivi","{h} h {m} min","{m} min","{s} s","{m} min {s} s","di {a}","Benvenuto in CheatRunner.","Ancora nessun file di cheat. Scarica cheat della community da Fonti oppure apri un gioco e trascina un file.","Annulla","Continua","Riconnessione...","Copiato","Copiato.","Sconosciuto","Finestra di dialogo","Disattivo","Privilegi","OK","Mancante","Non riuscito.","Avvio","Riprova","Gioco","Cerca cheat","Segui maj0r su X","Segui maj0r su Bluesky","Creato da maj0r - ð§ð·","Lascia una stella al progetto! â­","Cremisi","Autoload","Ripristina","Nessuna patch per questo gioco.","Metti le tue patch XML in /data/cheatrunner/patches/xml","Aggiornamento disponibile","La versione {v} Ã¨ disponibile.","Cerca cheat online","Cerca tutti i giochi con cheat e controlla se il tuo Ã¨ coperto.","Apri il sito","Repository di patch della community","Patch giÃ  pronte per i tuoi giochi. Non viene scaricato nulla: il repository si apre in una nuova scheda.","Apri PS-Game-Patch","Effetti visivi","Ridotti disattiva sfocature, ombre e animazioni. Ã l'impostazione predefinita nel browser della PS5.","Completi","Ridotti","Mostra altri ({n})","Consigliato","Release ufficiale di PS-Game-Patch. Le patch vengono salvate in /data/cheatrunner/patches.","Scarica le patch","Salvataggio dei file delle patch","Alcune patch funzionano solo se applicate all'avvio del gioco: salva il caricamento automatico e riavvia il gioco.","Risoluzione dei problemi","Segnala un problema o ripristina tutte le impostazioni ai valori predefiniti.","Copia la diagnostica e apre la pagina delle segnalazioni su GitHub.","Cheat, patch e profili di caricamento automatico non vengono toccati.","Tutto sembra a posto.","Qualcosa richiede attenzione: controlla i dettagli qui sotto.","Un file .mc4, .shn o .json dal tuo computer.","Aggiungine uno in File o Fonti","Aspetto","Aspetto, motore cheat e controller. Le modifiche vengono salvate subito.","File cheat","File cheat per questo gioco","CheatRunner elenca i giochi installati su questa console. Installane uno e aggiorna lâelenco.","Cheat e patch non possono essere applicati finchÃ© non viene attivato.","Controllo...","Scegli un altro file","Impossibile caricare","Impossibile caricare il file cheat","Impossibile caricare le patch","Impossibile caricare le impostazioni","Impossibile caricare i tuoi giochi","Impossibile leggere lo stato attuale dei cheat.","Scarica i cheat della community da Fonti o aggiungi un file dal tuo computer.","Scaricane uno da Fonti o trascina un file qui sotto.","Scarica cheat e patch pronti per i tuoi giochi. I download proseguono mentre navighi.","Scarica il set ufficiale di patch da Fonti oppure metti le tue patch XML in /data/cheatrunner/patches/xml.","Download in corso","Download {p}%","Download","Motore","Segue le ultime righe","Scarica cheat","Stato","Libreria","Stato in tempo reale del motore cheat e del patching della memoria, e cosa ha fatto CheatRunner.","NovitÃ , aiuto e le persone dietro CheatRunner.","Ancora nessun file cheat per questo gioco","Nessun cheat corrisponde a \"{q}\".","Nessun file","Nessun gioco trovato","Nessun file locale trovato","Nessuna patch per questo gioco","In esecuzione","Apri Impostazioni","In pausa mentre leggi","Preferenze","La modalitÃ  ridotta rimuove tutte le animazioni. Ã lâimpostazione predefinita nel browser della PS5.","Salvati in /data/cheatrunner/cheats. Il file contrassegnato In uso Ã¨ quello letto dal trainer.","Invia una segnalazione quando qualcosa non funziona.","Avvialo per usare i cheat","Avvia questo gioco per usare i suoi cheat.","Dettagli tecnici","Il motore cheat Ã¨ spento.","Non Ã¨ stato possibile leggere i valori attuali, quindi non viene mostrato nulla per evitare valori errati.","Lâessenziale per applicare cheat e patch.","Il file esiste ma non Ã¨ stato possibile leggerlo. Potrebbe essere danneggiato o ancora in copia.","Il file Ã¨ stato caricato ma Ã¨ vuoto. Prova un altro file per questo gioco.","Lâelenco dei giochi non Ã¨ disponibile al momento. Controlla che la console sia accesa e riprova.","Non Ã¨ stato possibile leggere i file patch di questo gioco. Riprova tra poco.","Gli interruttori potrebbero non essere aggiornati.","Tema, effetti e lingua di questa dashboard.","Questo file cheat non contiene cheat","Riprova qui sotto","Attivalo in Impostazioni","Zona pericolosa","Vai agli ultimi","Elimina il file in uso","Fonte cheat","Filtra giochi","Vedi i tuoi giochi","Crediti","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"ja-JP":["ååããã®ãã¼ãã§ã²ã¼ã ãã¯ã©ãã·ã¥ãã¾ããã","æå ±","ã¢ã¯ã·ã§ã³","ã¢ã¯ã·ã§ã³","ãã¡ã¤ã«ãè¿½å ","ãã¼ããè¿½å ","ãæ°ã«å¥ãã«è¿½å ","åã²ã¼ã ã®ãªãã·ã§ã³ã¡ãã¥ã¼ã« â CheatRunner é ç®ãè¿½å ããå®è¡ä¸­ã®ã²ã¼ã ã®ãã¬ã¼ãã¼ãéããã¿ã³ã®çµã¿åãããæä¾ãã¾ãã","è©³ç´°è¨­å®","ãã¹ã¦","ãã¹ã¦ã®æ¯æ´ã¯ãç ç©¶ã»éçºã»ä»å¾ã®ã¢ããã°ã¬ã¼ãã®ãããä½ãã¡ã¼ã ã¦ã§ã¢ï¼1.xxâ4.xxï¼ã®PS5ãå¥æããç®æ¨ã«ä½¿ããã¾ãã","ãã¹ã¦ã®ãã¦ã³ã­ã¼ãã«å¤±æãã¾ãã","ãã¹ã¦ã®ã²ã¼ã ","å¤æ´ããããã¡ã¤ã«ãæ´æ°ãã","é©ç¨æ¸ã¿","é©ç¨","ã¢ããª","èªåé¸æ","æ»ã","ã©ã¤ãã©ãªãè¦ã","ãã¿ã³","Buy Me a Coffee","ãã¼ã","ãã¼ãã¨ã³ã¸ã³","CheatRunner - ããªãã®PS5ã®ããã®ãã¼ããã¬ã¼ãã¼ï¼","ãã¼ã","ã²ã¼ã èµ·åå¾ããã®æéã ããã¼ãã®é©ç¨ãå¾æ©ãã¾ãã","ãã¼ããããåé ­ã«","ãããã¯ã¼ã¯ãé¸æ","Circle","ãã£ã«ã¿ã¼ãã¯ãªã¢","ãã©ã°ãã¯ãªã¢","æ¤ç´¢ãã¯ãªã¢","éãã","æãããã","ã¡ãã¥ã¼ãæãããã","ãã®ããã·ã¥ãã¼ãã®éè²ã","ã³ãã¥ããã£","ã³ãã¥ããã£ã®ãã¼ããªãã¸ããª","ã³ã³ãã­ã¼ã©ã¼ã®ã·ã§ã¼ãã«ããã¨ã²ã¼ã ã¡ãã¥ã¼","ã³ãã¼","URLãã³ãã¼","ãã¹ã¦ã³ãã¼","ãã¼ããã©ã«ãã¼ãã³ãã¼","ãªã³ã¯ãã³ãã¼","ã¯ã©ãã·ã¥æ¤åº","ååã²ã¼ã ãã¯ã©ãã·ã¥ãã¾ãã","Cross","æå·è³ç£","æå·è³ç£ã§ã®æ¯æ´","ãã¼ã¯","åé¤","ãã¹ã¦ãªã","ä¸","ãã¹ã¦ã®ã½ã¼ã¹ããã¦ã³ã­ã¼ã","ã³ãã¥ããã£ã®ãã¼ãããã¦ã³ã­ã¼ã","ãã¦ã³ã­ã¼ãã«å¤±æãã¾ãã","ã²ã¼ã ç¨ã®æ¢æãã¼ããã¡ã¤ã«ããã¦ã³ã­ã¼ããã¾ãã/data/cheatrunner/cheats ã«ä¿å­ããããã¦ã³ã­ã¼ãã¯ããã¯ã°ã©ã¦ã³ãã§ç¶è¡ããã¾ãã","é¸æããã¦ã³ã­ã¼ã","ãã¦ã³ã­ã¼ãæ¸ã¿",".mc4ã.shnã.json ãã¡ã¤ã«ãããã«ãã­ããããããã¯ãªãã¯ãã¦é¸æ","ã³ã³ãã­ã¼ã©ã¼ã®ã·ã§ã¼ãã«ãããæå¹ã«ãã","ã·ã§ã¼ãã«ãããæå¹ã«ãã","ã¨ã³ã¸ã³ ãªã³","Enterã§éã","ã¨ã©ã¼ã®ã¿","å®å¨ä¸è´","å¤±æ","ãæ°ã«å¥ã","ãã¡ã¤ã«","ãã£ã«ã¿ã¼","ã­ã°ããã£ã«ã¿ã¼","ãã¼ãããªã³ã«ããç´å¾ã«ã²ã¼ã ãçµäºããå ´åããã®ãã¼ãã«å°ãä»ãã¾ãã","ã¦ã¼ã¶ã¼ã®ããã«ãã¦ã¼ã¶ã¼ã«ãã£ã¦ã","å¼·å¶é©ç¨","ã²ã¼ã ","ãã¼ãã®ããã²ã¼ã ","ãããã®ããã²ã¼ã ","ãã¼ãéã®éé (ms)","ä¸è¬","åæã«æ¼ãç¶ããã¨ã·ã§ã¼ãã«ãããä½åãã¾ãã","é·æ¼ãæé (ms)","2ã¤ã®ãã¿ã³ãåæã«æ¼ãç¶ããã¨ãå®è¡ä¸­ã®ã²ã¼ã ã®ãã¬ã¼ãã¼ãéãã¾ãã","ãã¼ã ç»é¢ã®ã¿ã¤ã«","ã·ã¹ãã UIã«ããã¯ãã¾ããããã©ã«ãã¯ãªãããã¡ã¼ã ã¦ã§ã¢ã«ãã£ã¦ã¯ãã¼ã ç»é¢ãã¯ã©ãã·ã¥ãããã¨ãããã¾ãã","ã³ã³ããé·æ¼ããã¦ä½åããã¾ã§ã®æéã","ã­ã°ã«æ¸ãè¾¼ãè©³ç´°åº¦ã","CheatRunner ãå½¹ã«ç«ã£ãããéçºãæ¯æ´ãã¦ããã ããã¨å¬ããã§ãï¼ð«¶","ä½¿ç¨ä¸­","èµ·åæã«PS5ã®ãã¼ã ç»é¢ã«CheatRunnerã®ã¿ã¤ã«ãã¤ã³ã¹ãã¼ã«ãã¾ãã","Discordã«åå ","Ko-Fiãã³ã¼ãã¼ãæå·è³ç£ãPIX","è¨èª","ãã®ããã·ã¥ãã¼ãã®è¨èªã","æçµãã¬ã¤","å·¦","ã©ã¤ã","ãã¼ãã¨ã³ã¸ã³ã¨ã¡ã¢ãªãããã®ç¶æã","ã­ã¼ã«ã«ã®ãã¼ããã¡ã¤ã«","ã­ã°ã¬ãã«","ã­ã°ã¬ãã«ã®ãã£ã«ã¿ã¼","ã­ã°","ã¡ã¤ã³","ãã¼ãã¨ããããé©ç¨ããããã®ã¡ã¤ã³ã¹ã¤ããã","ãã¹ã¿ã¼ã³ã¼ãè£æ­£","ãã®ã²ã¼ã ã®ãã¼ã¸ã§ã³ã¨ä¸è´ããªãå¯è½æ§ãããã¾ã","ã¡ã¢ãªããã","ããããã¤ã","2åã®ãã¼ãåãæ¿ãã®æå°ééã","åå A-Z","åå Z-A","ãã®ã²ã¼ã ã®ãã¼ããã¡ã¤ã«ã¯ã¾ã ããã¾ããã","ãæ°ã«å¥ãã¯ã¾ã ããã¾ãããã²ã¼ã ã®æãã¿ãããã¦è¿½å ãã¾ãã","å®è¡ãã¦ãã¾ãã","ãªã³","ã¹ãã¼ããã©ã³ã§éã","ã¹ãã¼ããã©ã³ã§éãï¼QRã³ã¼ãï¼","ã¹ãã¼ããã©ã³ã§ããã·ã¥ãã¼ããéã","ãã¬ã¼ãã¼ãéã","ãªãã·ã§ã³","å¥ã®ãã¼ã¸ã§ã³","ãã¼ã¸","ããã","ãã¬ã¤","QRã³ã¼ã","ããã·ã¥ãã¼ãã®ã¢ãã¬ã¹ã®QRã³ã¼ã","ãã®ã²ã¼ã ã®èµ·åãã¨ã«ããªã³ã®ãã¼ããåé©ç¨ãã","æºåå®äº","æè¿","æ´æ°","ã²ã¼ã ä¸è¦§ãæ´æ°","åèµ·å","åé¡ãå ±å","ã¤ã³ã¹ãã¼ã«æ¸ã¿ã²ã¼ã ãåã¹ã­ã£ã³","åæè¨­å®ã«æ»ã","ãã¹ã¿ã¼ã³ã¼ãã«ä¾å­ãããã¼ããèªåçã«è§£æ±ºãã¾ãã","åã«æ»ã","å³","å®è¡ä¸­","ç¾å¨å®è¡ä¸­","ãªã¼ãã­ã¼ããä¿å­","ã¹ãã¼ããã©ã³ã®ã«ã¡ã©ã§èª­ã¿åã£ã¦ãã ãããã¹ãã¼ããã©ã³ã¯PS5ã¨åãWi-Fiã«æ¥ç¶ããã¦ããå¿è¦ãããã¾ãã","æ¤ç´¢","ååã¾ãã¯ã¿ã¤ãã«IDã§æ¤ç´¢","ã²ã¼ã ãæ¤ç´¢","ã²ã¼ã ããã¼ã¸ãæä½ãæ¤ç´¢","ã¢ãã¬ã¹ãé¸ã¶ã¨å¨ä½ãã³ãã¼ãã¾ãã","è¨­å®","QRã³ã¼ããè¡¨ç¤º","ãã¤ã­ã¼ããçµäº","ã²ã¼ã ã®ä¸¦ã¹æ¿ã","ã½ã¼ã¹","ã½ã¼ã¹","éåº¦","Square","ã²ã¼ã ãèµ·åãã¦ãã ãã","ããããé©ç¨ããã«ã¯ããã®ã²ã¼ã ãèµ·åãã¦ãã ããã","ã¹ãã¼ã¿ã¹","ã¹ãã¼ã¿ã¹ã¨ã­ã°","CheatRunnerãæ¯æ´","ãã©ã¸ã«ããã®æ¯æ´","ãã®ãã­ã¸ã§ã¯ããæ¯æ´","ãµãã¼ã¿ã¼","ã·ã¹ãã ","åºæ¬è¨­å® - ãã¼ãã¨ãã¼ãã¨ã³ã¸ã³ã®ã¹ã¤ããã","ãã¼ã","ãã¼ãã¨ãã¼ãã¨ã³ã¸ã³","æ®ãæé","ãã¬ã¤æé","ã¿ã¤ãã«ID","åãæ¿ã","TouchPad","ãã¬ã¼ãã¼","Triangle","ä¸","ææ°","ä½¿ç¨","ãã¼ã¸ã§ã³","ã²ã¼ã èµ·åå¾ã®å¾æ© (ms)","è­¦åã¨ã¨ã©ã¼","CheatRunnerã®åä½è¨é² - ãã¾ãåããªãã¨ãã«å½¹ç«ã¡ã¾ãã","ãã¼ããã","ââ ç§»å","ãã¹ã¦ã®è¨­å®ãåæå¤ã«æ»ãã¾ããWebãã¼ãã¯ç¶­æããã¾ãã","å¥ã®ãã¼ããé©ç¨ä¸­ã§ãããã°ãããã¦ããããä¸åº¦ãè©¦ããã ããã","ä»»æã®ãã¼ã¸ã§ã³","ããã§ãé©ç¨","èªåé¸æãå¾©åãã¾ããã","ãªã¼ãã­ã¼ããè§£é¤ãã¾ããã","ãªã¼ãã­ã¼ããä¿å­ãã¾ãã ({n})ãã²ã¼ã èµ·åæã«ãªã³ã«ãªãã¾ãã","ç©ºã®ãªã¼ãã­ã¼ããä¿å­ãã¾ããã","ãã¼ã #{n}","ãã¼ããã¡ã¤ã«ãè¿½å ãã¾ããã","ãã¼ããã¡ã¤ã«ãåé¤ãã¾ããã","ãã¼ããã¡ã¤ã«ãé¸æãã¾ããã","èµ·åä¸­ã«CheatRunnerãå¿ç­ããªããªãã¾ããã","ãã¼ããã©ã«ãã¼ãã³ãã¼ãã¾ããã","{name} ãç¢ºèªä¸­","ãªã¼ãã­ã¼ããè§£é¤ ({n})","ãã¡ã¤ã«ã1ã¤ãã¤æ¯è¼ãã¦ãã¦ã³ã­ã¼ãä¸­","å¥ã®ãã¼ãã¨ç«¶åãã¾ã","ã³ãã¼ã«å¤±æãã¾ããããã­ã¹ããæåã§é¸æãã¦ãã ããã","ãªã¼ãã­ã¼ããè§£é¤ã§ãã¾ããã§ããã","è¨ºæ­æå ±ãã³ãã¼ã§ãã¾ããã§ããã","ãã¼ãããªãã«ã§ãã¾ããã§ããã","ããããèª­ã¿è¾¼ãã¾ããã§ããã","ã²ã¼ã ä¸è¦§ãèª­ã¿è¾¼ãã¾ããã§ããã","ãªã¼ãã­ã¼ããä¿å­ã§ãã¾ããã§ããã","ä¿å­ã§ãã¾ããã§ãããããä¸åº¦ãè©¦ããã ããã","ãã¡ã¤ã«ãé¸æã§ãã¾ããã§ããã","ãã¦ã³ã­ã¼ããéå§ã§ãã¾ããã§ããã","éå§ã§ãã¾ããã§ããã","ãã¼ããåãæ¿ãããã¾ããã§ããã","åé¤ã«å¤±æãã¾ããã","ãã¡ã¤ã«ãåé¤","{file} ãåé¤ãã¾ããï¼","è¨ºæ­æå ±ãã³ãã¼ãã¾ãããissueã«è²¼ãä»ãã¦ãã ããã","{name} ããã¦ã³ã­ã¼ãä¸­","ã¨ã³ã¸ã³ ãªã","ãã¹ã¦ææ°ã§ã","ã¡ãã¥ã¼ãå±é","ãªãã¸ããªå¨ä½ãä¸æ¬ã§åå¾ä¸­","ãã¡ã¤ã« v{a} / ã²ã¼ã  v{b}","{name} ãå®äº","{name} ãå¼·å¶é©ç¨ãã¾ããï¼","ã²ã¼ã ãèµ·åãã¾ããã","å¥ã®ã²ã¼ã ãã¼ã¸ã§ã³åãã®ãããã²ã¼ã ãã¯ã©ãã·ã¥ããå¯è½æ§ãããã¾ãããåã«æ»ããã§åã®ãã¤ãã«æ»ãã¾ãã","èµ·åã«å¤±æãã¾ããã","èµ·åã«å¤±æãã¾ãã: {msg}","èµ·åãªã¯ã¨ã¹ãã«å¤±æãã¾ãããã²ã¼ã ã¯ã¾ã èµ·åä¸­ããããã¾ããã","èµ·åãã¿ã¤ã ã¢ã¦ããã¾ãããã²ã¼ã ãèµ·åãããç¢ºèªãã¦ãã ããã","{name} ãèµ·åä¸­...","ã­ã°ãã³ãã¼ãã¾ããã","maj0r ã {heart} ãè¾¼ãã¦å¶ä½","v{v} ã¨ä¸è´","v{v} ãå¿è¦","ã²ã¼ã ãªã","æ¤ç´¢ã«ä¸è´ããã²ã¼ã ã¯ããã¾ããã","ãã¼ãã®ããã²ã¼ã ã¯ã¾ã ããã¾ãããã½ã¼ã¹ããå¥æãã¦ãã ããã","ã­ã°ã¯ã¾ã ããã¾ããã","ä¸è´ããè¡ã¯ããã¾ããã","æªæºå","æè¿ã®é ç®ã¯ããã¾ãããã²ã¼ã ãèµ·åãããããã®ãã¬ã¼ãã¼ãéãã¦ãã ããã","ã³ãã¼ãããã®ãããã¾ããã","ãªã³ã®ãã®ã¯ããã¾ããã§ããã","ãã¬ã¼ãã¼ãéã: {name}","ãããã®ãªã¼ãã­ã¼ããè§£é¤ãã¾ããã","ãããã®ãªã¼ãã­ã¼ããä¿å­ãã¾ããã","QRã³ã¼ããå©ç¨ã§ãã¾ããã","GitHubãããã¡ã¤ã«ä¸è¦§ãèª­ã¿è¾¼ã¿ä¸­","ãæ°ã«å¥ãããåé¤","ãªã»ããã«å¤±æãã¾ããã","è¨­å®ããªã»ãããã¾ããï¼","ä¿å­ãã¾ãã","ãã¼ããã¡ã¤ã«ãä¿å­ä¸­","ä¿å­ä¸­...","ãã®å¾ãPCããæ°ããCheatRunner.elfãéä¿¡ãã¦ãã ããã","è¨­å®ããªã»ãããã¾ããã","çµäº","CheatRunnerãçµäºãã¾ããï¼","ã½ã¼ã¹ {i}/{n}","åã«ã²ã¼ã ãèµ·åãã¦ãã ããã","ã²ã¼ã ã¯ã¾ã èª­ã¿è¾¼ã¿ä¸­ã§ãã","ã²ã¼ã ã¯ã¾ã èµ·åä¸­ã§ããæ°ç§å¾ã«ããä¸åº¦ãè©¦ããã ããã","ã²ã¼ã ä¸è¦§ã¯ç¾å¨å©ç¨ã§ãã¾ããã","ãã®ãã¼ãã¯ç¾å¨åãæ¿ãããã¾ããã","ãã®ãã¼ãã¯ä»¥åã²ã¼ã ãã¯ã©ãã·ã¥ããã¾ãããåè©¦è¡ããã«ã¯ãã©ã°ãã¯ãªã¢ãã¦ãã ããã","ãã®ãã¡ã¤ã«ã¯ã¤ã³ã¹ãã¼ã«æ¸ã¿ã¨ã¯å¥ã®ã²ã¼ã ãã¼ã¸ã§ã³ç¨ã§ãã","ã³ã³ã½ã¼ã«ãããã®ãã¡ã¤ã«ãåé¤ãã¾ããã½ã¼ã¹ããåãã¦ã³ã­ã¼ãã§ãã¾ãã","æè¦æé {t}","URLãã³ãã¼ãã¾ããã","{name} ãå±éä¸­","éå¯¾å¿ã®ããã","ã¢ããã­ã¼ãã«å¤±æãã¾ããã",".mc4ã.shnã.json ã®ãã¡ã¤ã«ãä½¿ç¨ãã¦ãã ããã","å¥ã®ãã¼ã¸ã§ã³ãä½¿ç¨ãã¾ããï¼","ããã§ãä½¿ç¨","CheatRunnerã®å¾©å¸°ãå¾ã£ã¦ãã¾ã...","{label} ã®ã¢ãã¬ã¹ãã³ãã¼ãã¾ãã","ãã¹ã¦ã®ã½ã¼ã¹","å¹³å {s}","ãã£ãä»","ãã¼ããã¡ã¤ã«","v{v} ã¨ä¸è´","{n} åã®ãã¼ãããªãã«ãã¾ããã","æ°è¦ã¾ãã¯æ´æ°ããããã¡ã¤ã«ã {n} åä¿å­ãã¾ãã","{n} æ¬","{n} æ¬","{n}æ¥å","{n}æéå","{n}åå","{page} - CheatRunner","{n} ä»¶ä¸­ {a} ä»¶ãªã³","{h}æé{m}å","{m}å","{s}ç§","{m}å{s}ç§","ä½è: {a}","CheatRunnerã¸ããããã","ãã¼ããã¡ã¤ã«ã¯ã¾ã ããã¾ãããã½ã¼ã¹ããã³ãã¥ããã£ã®ãã¼ããå¥æããããã²ã¼ã ãéãã¦ãã¡ã¤ã«ããã­ãããã¦ãã ããã","ã­ã£ã³ã»ã«","ç¶è¡","åæ¥ç¶ä¸­...","ã³ãã¼ãã¾ãã","ã³ãã¼ãã¾ããã","ä¸æ","ãã¤ã¢ã­ã°","ãªã","æ¨©é","OK","ä¸è¶³","å¤±æãã¾ããã","èµ·åä¸­","åè©¦è¡","ã²ã¼ã ","ãã¼ããæ¤ç´¢","Xã§maj0rããã©ã­ã¼","Blueskyã§maj0rããã©ã­ã¼","å¶ä½: maj0r - ð§ð·","ãã­ã¸ã§ã¯ãã«ã¹ã¿ã¼ããé¡ããã¾ãï¼â­","ã¯ãªã ã¾ã³","ãªã¼ãã­ã¼ã","ãªã»ãã","ãã®ã²ã¼ã ã®ãããã¯ããã¾ããã","XMLãããã /data/cheatrunner/patches/xml ã«ç½®ãã¦ãã ãã","ã¢ãããã¼ããã","ãã¼ã¸ã§ã³ {v} ãå©ç¨å¯è½ã§ãã","ãã¼ãããªã³ã©ã¤ã³ã§æ¢ã","ãã¼ãããããã¹ã¦ã®ã²ã¼ã ãæ¤ç´¢ãã¦ããæã¡ã®ã²ã¼ã ãå¯¾å¿ãã¦ãããç¢ºèªã§ãã¾ãã","ãµã¤ããéã","ã³ãã¥ããã£ãããã®ãªãã¸ããª","ã²ã¼ã ç¨ã®ãããéã§ãããã¦ã³ã­ã¼ãã¯ãããããªãã¸ããªãæ°ããã¿ãã§éãã¾ãã","PS-Game-Patch ãéã","è¦è¦å¹æ","ãè»½éãã«ããã¨ã¼ãããå½±ãã¢ãã¡ã¼ã·ã§ã³ãç¡å¹ã«ãªãã¾ããPS5 ã®ãã©ã¦ã¶ã§ã¯åæè¨­å®ã§ãã","æ¨æº","è»½é","ããã«è¡¨ç¤º ({n})","ãããã","PS-Game-Patch ã®å¬å¼ãªãªã¼ã¹ã§ãããããã¯ /data/cheatrunner/patches ã«ä¿å­ããã¾ãã","ãããããã¦ã³ã­ã¼ã","ããããã¡ã¤ã«ãä¿å­ãã¦ãã¾ã","ä¸é¨ã®ãããã¯ã²ã¼ã ã®èµ·åæã«é©ç¨ããªãã¨åä½ãã¾ãããèªåèª­ã¿è¾¼ã¿ãä¿å­ãã¦ã²ã¼ã ãåèµ·åãã¦ãã ããã","ãã©ãã«ã·ã¥ã¼ãã£ã³ã°","åé¡ãå ±åãããããã¹ã¦ã®è¨­å®ãåæå¤ã«æ»ãã¾ãã","è¨ºæ­æå ±ãã³ãã¼ããGitHub ã® Issue ãã¼ã¸ãéãã¾ãã","ãã¼ããããããèªåèª­ã¿è¾¼ã¿ãã­ãã¡ã¤ã«ã¯å¤æ´ããã¾ããã","åé¡ããã¾ããã","ç¢ºèªãå¿è¦ãªé ç®ãããã¾ããä¸ã®è©³ç´°ããè¦§ãã ããã","ãã½ã³ã³ã® .mc4ã.shnã.json ãã¡ã¤ã«ã","ããã¡ã¤ã«ãã¾ãã¯ãã½ã¼ã¹ãã§è¿½å ","å¤è¦³","å¤è¦³ããã¼ãã¨ã³ã¸ã³ãã³ã³ãã­ã¼ã©ã¼ã®è¨­å®ãå¤æ´ã¯ããã«ä¿å­ããã¾ãã","ãã¼ããã¡ã¤ã«","ãã®ã²ã¼ã ã®ãã¼ããã¡ã¤ã«","CheatRunner ã¯ããã®ã³ã³ã½ã¼ã«ã«ã¤ã³ã¹ãã¼ã«ãããã²ã¼ã ãè¡¨ç¤ºãã¾ããã²ã¼ã ãã¤ã³ã¹ãã¼ã«ãã¦ä¸è¦§ãæ´æ°ãã¦ãã ããã","ãªã³ã«ããã¾ã§ããã¼ãããããã¯é©ç¨ã§ãã¾ããã","ç¢ºèªä¸­...","å¥ã®ãã¡ã¤ã«ãé¸ã¶","èª­ã¿è¾¼ãã¾ãã","ãã¼ããã¡ã¤ã«ãèª­ã¿è¾¼ãã¾ããã§ãã","ããããèª­ã¿è¾¼ãã¾ããã§ãã","è¨­å®ãèª­ã¿è¾¼ãã¾ããã§ãã","ã²ã¼ã ãèª­ã¿è¾¼ãã¾ããã§ãã","ãã¼ãã®ç¾å¨ã®ç¶æãèª­ã¿åãã¾ããã§ããã","ãã½ã¼ã¹ãããã³ãã¥ããã£ã®ãã¼ãããã¦ã³ã­ã¼ããããããã½ã³ã³ã®ãã¡ã¤ã«ãè¿½å ãã¦ãã ããã","ãã½ã¼ã¹ããããã¦ã³ã­ã¼ãããããä¸ã«ãã¡ã¤ã«ããã­ãããã¦ãã ããã","ã²ã¼ã ç¨ã®æ¢è£½ãã¼ãã¨ãããããã¦ã³ã­ã¼ããã¾ããã»ãã®ç»é¢ãè¦ã¦ããéããã¦ã³ã­ã¼ãã¯ç¶ãã¾ãã","ãã½ã¼ã¹ãããå¬å¼ãããä¸å¼ããã¦ã³ã­ã¼ãããããXML ãããã /data/cheatrunner/patches/xml ã«ç½®ãã¦ãã ããã","ãã¦ã³ã­ã¼ãä¸­","ãã¦ã³ã­ã¼ãä¸­ {p}%","ãã¦ã³ã­ã¼ã","ã¨ã³ã¸ã³","ææ°ã®è¡ãè¿½å¾ä¸­","ãã¼ããå¥æ","ç¶æ","ã©ã¤ãã©ãª","ãã¼ãã¨ã³ã¸ã³ã¨ã¡ã¢ãªãããã®ãªã¢ã«ã¿ã¤ã ã®ç¶æã¨ãCheatRunner ã®åä½è¨é²ã§ãã","ãã¥ã¼ã¹ããµãã¼ããããã¦ CheatRunner ãä½ã£ã¦ããäººãã¡ã","ãã®ã²ã¼ã ã®ãã¼ããã¡ã¤ã«ã¯ã¾ã ããã¾ãã","ã{q}ãã«ä¸è´ãããã¼ãã¯ããã¾ããã","ãã¡ã¤ã«ãªã","ã²ã¼ã ãè¦ã¤ããã¾ãã","ã­ã¼ã«ã«ãã¡ã¤ã«ãè¦ã¤ããã¾ãã","ãã®ã²ã¼ã ã®ãããã¯ããã¾ãã","ãã¬ã¤ä¸­","è¨­å®ãéã","é²è¦§ä¸­ã¯ä¸æåæ­¢","ç°å¢è¨­å®","ãè»½éãã§ã¯ãã¹ã¦ã®ã¢ãã¡ã¼ã·ã§ã³ãç¡å¹ã«ãã¾ããPS5 ã®ãã©ã¦ã¶ã¼ã§ã¯æ¢å®ã§ãã","/data/cheatrunner/cheats ã«ä¿å­ããã¾ãããä½¿ç¨ä¸­ãã¨è¡¨ç¤ºããããã¡ã¤ã«ããã¬ã¼ãã¼ãèª­ã¿è¾¼ã¿ã¾ãã","ãã¾ãåããªãã¨ãã¯å ±åãéã£ã¦ãã ããã","èµ·åããã¨ãã¼ããä½¿ãã¾ã","ãã®ã²ã¼ã ãèµ·åããã¨ãã¼ããä½¿ãã¾ãã","æè¡çãªè©³ç´°","ãã¼ãã¨ã³ã¸ã³ã¯ãªãã§ãã","ç¾å¨ã®å¤ãèª­ã¿åããªãã£ããããèª¤ã£ãå¤ãè¡¨ç¤ºããªãããä½ãè¡¨ç¤ºãã¦ãã¾ããã","ãã¼ãã¨ããããé©ç¨ããããã®åºæ¬è¨­å®ã§ãã","ãã¡ã¤ã«ã¯ããã¾ãããèª­ã¿åãã¾ããã§ãããç ´æãã¦ããããã³ãã¼ä¸­ã®å¯è½æ§ãããã¾ãã","ãã¡ã¤ã«ã¯èª­ã¿è¾¼ãã¾ããããä¸­èº«ãç©ºã§ãããã®ã²ã¼ã ç¨ã®å¥ã®ãã¡ã¤ã«ãè©¦ãã¦ãã ããã","ã²ã¼ã ä¸è¦§ãåå¾ã§ãã¾ãããã³ã³ã½ã¼ã«ãèµ·åãã¦ãããç¢ºèªãã¦ãããä¸åº¦ãè©¦ããã ããã","ãã®ã²ã¼ã ã®ããããã¡ã¤ã«ãèª­ã¿åãã¾ããã§ããããã°ãããã¦ããããä¸åº¦ãè©¦ããã ããã","ã¹ã¤ããã®è¡¨ç¤ºãå¤ãå¯è½æ§ãããã¾ãã","ãã®ããã·ã¥ãã¼ãã®ãã¼ããå¹æãè¨èªã","ãã®ãã¼ããã¡ã¤ã«ã«ã¯ãã¼ããããã¾ãã","ä¸ã§åè©¦è¡ãã¦ãã ãã","è¨­å®ã§ãªã³ã«ãã¦ãã ãã","å±éºã¾ã¼ã³","ææ°ã¸ç§»å","ä½¿ç¨ä¸­ã®ãã¡ã¤ã«ãåé¤","ãã¼ãã®ã½ã¼ã¹","ã²ã¼ã ãçµãè¾¼ã","ã²ã¼ã ãè¦ã","ã¯ã¬ã¸ãã","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"ko-KR":["ì§ëë²ì ì´ ì¹í¸ë¡ ê²ìì´ ì¶©ëíìµëë¤.","ì ë³´","ëì","ëì","íì¼ ì¶ê°","ì¹í¸ ì¶ê°","ì¦ê²¨ì°¾ê¸°ì ì¶ê°","ê° ê²ìì ìµì ë©ë´ì â CheatRunner í­ëª©ì ì¶ê°íê³ , ì¤í ì¤ì¸ ê²ìì í¸ë ì´ëë¥¼ ì¬ë ë²í¼ ì¡°í©ì ì ê³µí©ëë¤.","ê³ ê¸","ì ì²´","ëª¨ë  íìê¸ì ì°êµ¬, ê°ë° ë° í¥í ìê·¸ë ì´ëë¥¼ ìí ì íì¨ì´(1.xxâ4.xx) PS5 êµ¬ì ëª©íì ì¬ì©ë©ëë¤.","ëª¨ë  ë¤ì´ë¡ëì ì¤í¨íìµëë¤","ëª¨ë  ê²ì","ë³ê²½ë íì¼ë ìë°ì´í¸","ì ì©ë¨","ì ì©","ì±","ìë ì í","ë¤ë¡","ë¼ì´ë¸ë¬ë¦¬ ëë¬ë³´ê¸°","ë²í¼","Buy Me a Coffee","ì¹í¸","ì¹í¸ ìì§","CheatRunner - PS5ì© ì¹í¸ í¸ë ì´ë!","ì¹í¸","ê²ì ìì í ì´ ìê° ëì ì¹í¸ ì ì©ì ë³´ë¥í©ëë¤.","ì¹í¸ ìë ê²ì ë¨¼ì ","ë¤í¸ìí¬ ì í","Circle","íí° ì§ì°ê¸°","íì ì§ì°ê¸°","ê²ì ì§ì°ê¸°","ë«ê¸°","ì ê¸°","ë©ë´ ì ê¸°","ì´ ëìë³´ëì ìì íë§ìëë¤.","ì»¤ë®¤ëí°","ì»¤ë®¤ëí° ì¹í¸ ì ì¥ì","ì»¨í¸ë¡¤ë¬ ë¨ì¶í¤ ë° ê²ì ë©ë´","ë³µì¬","URL ë³µì¬","ëª¨ë ë³µì¬","ì¹í¸ í´ë ë³µì¬","ë§í¬ ë³µì¬","ì¶©ë ê°ì§","ì§ëë²ì ê²ìì´ ì¶©ëíìµëë¤","Cross","ìí¸íí","ìí¸íí íì","ë¤í¬","ì­ì ","ëª¨ë ëê¸°","ìë","ëª¨ë  ìì¤ ë¤ì´ë¡ë","ì»¤ë®¤ëí° ì¹í¸ ë¤ì´ë¡ë","ë¤ì´ë¡ë ì¤í¨","ê²ìì© ì¹í¸ íì¼ì ë°ë¡ ë´ë ¤ë°ìµëë¤. /data/cheatrunner/cheatsì ì ì¥ëë©° ë¤ì´ë¡ëë ë°±ê·¸ë¼ì´ëìì ê³ìë©ëë¤.","ì í í­ëª© ë¤ì´ë¡ë","ë¤ì´ë¡ëë¨",".mc4, .shn ëë .json íì¼ì ì¬ê¸°ì ëê±°ë í´ë¦­í´ì ì°¾ìë³´ì¸ì","ì»¨í¸ë¡¤ë¬ ë¨ì¶í¤ ì¬ì©","ë¨ì¶í¤ ì¬ì©","ìì§ ì¼ì§","Enter ì´ê¸°","ì¤ë¥ë§","ì íí ì¼ì¹","ì¤í¨","ì¦ê²¨ì°¾ê¸°","íì¼","íí°","ë¡ê·¸ íí°","ì¹í¸ë¥¼ ì¼  ì§í ê²ìì´ ì¢ë£ëë©´ í´ë¹ ì¹í¸ì íìí©ëë¤.","ì¬ì©ìë¥¼ ìí´, ì¬ì©ìì í¨ê».","ê°ì  ì ì©","ê²ì","ì¹í¸ê° ìë ê²ì","í¨ì¹ê° ìë ê²ì","ì¹í¸ ê° ê°ê²© (ms)","ì¼ë°","í¨ê» ëë¬ ë¨ì¶í¤ë¥¼ ì¤íí©ëë¤.","ëë¥´ë ìê° (ms)","ë ë²í¼ì í¨ê» ëë¥´ê³  ìì¼ë©´ ì¤í ì¤ì¸ ê²ìì í¸ë ì´ëê° ì´ë¦½ëë¤.","í íë©´ íì¼","ìì¤í UIì ì°ê²°ë©ëë¤. ê¸°ë³¸ê°ì êº¼ì§ì´ë©°, ì¼ë¶ íì¨ì´ììë í íë©´ì´ ì¶©ëí  ì ììµëë¤.","ì¡°í©ì ì¼ë§ë ì¤ë ëë¬ì¼ ì¤íëëì§ ì í©ëë¤.","ë¡ê·¸ì ê¸°ë¡í  ìì¸ ìì¤ìëë¤.","CheatRunnerê° ëìì´ ëìë¤ë©´ íë¡ì í¸ ê°ë°ì íìí´ ì£¼ì¸ì! ð«¶","ì¬ì© ì¤","ë¶í ì PS5 í íë©´ì CheatRunner íì¼ì ì¤ì¹í©ëë¤.","Discord ì°¸ì¬","Ko-Fi, ì»¤í¼, ìí¸íí, PIX","ì¸ì´","ì´ ëìë³´ëì ì¸ì´ìëë¤.","ë§ì§ë§ íë ì´","ì¼ìª½","ë¼ì´í¸","ì¹í¸ ìì§ê³¼ ë©ëª¨ë¦¬ í¨ì¹ì ìíìëë¤.","ë¡ì»¬ ì¹í¸ íì¼","ë¡ê·¸ ìì¤","ë¡ê·¸ ìì¤ íí°","ë¡ê·¸","ë©ì¸","ì¹í¸ì í¨ì¹ ì ì©ì ìí ë§ì¤í° ì¤ìì¹ìëë¤.","ë§ì¤í° ì½ë ë³´ì ","ì´ ê²ì ë²ì ê³¼ ë§ì§ ìì ì ììµëë¤","ë©ëª¨ë¦¬ í¨ì¹","ë¯¸ëëì´í¸","ë ì¹í¸ ì í ì¬ì´ì ìµì ìê°ìëë¤.","ì´ë¦ A-Z","ì´ë¦ Z-A","ì´ ê²ìì ì¹í¸ íì¼ì´ ìì§ ììµëë¤.","ìì§ ì¦ê²¨ì°¾ê¸°ê° ììµëë¤. ê²ìì ë³ì ëë¬ ì¶ê°íì¸ì.","ì¤í ì¤ ìë","ì¼ì§","í´ëí°ìì ì´ê¸°","í´ëí°ìì ì´ê¸° (QR ì½ë)","í´ëí°ìì ëìë³´ë ì´ê¸°","í¸ë ì´ë ì´ê¸°","ìµì","ë¤ë¥¸ ë²ì ","íì´ì§","í¨ì¹","íë ì´","QR ì½ë","ëìë³´ë ì£¼ì QR ì½ë","ì´ ê²ìì ì¤íí  ëë§ë¤ ì¼ì§ ì¹í¸ë¥¼ ë¤ì ì ì©í©ëë¤","ì¤ë¹ë¨","ìµê·¼","ìë¡ê³ ì¹¨","ê²ì ëª©ë¡ ìë¡ê³ ì¹¨","ë¤ì ì¤í","ë¬¸ì  ì ê³ ","ì¤ì¹ë ê²ì ë¤ì ê²ì","ê¸°ë³¸ê°ì¼ë¡ ì´ê¸°í","ë§ì¤í° ì½ëì ìì¡´íë ì¹í¸ë¥¼ ìëì¼ë¡ ì²ë¦¬í©ëë¤.","ë³µì","ì¤ë¥¸ìª½","ì¤í ì¤","ì§ê¸ ì¤í ì¤","ìë ë¡ë ì ì¥","í´ëí° ì¹´ë©ë¼ë¡ ì¤ìºíì¸ì. í´ëí°ì´ PS5ì ê°ì Wi-Fiì ì°ê²°ëì´ ìì´ì¼ í©ëë¤.","ê²ì","ì´ë¦ ëë íì´í IDë¡ ê²ì","ê²ì ê²ì","ê²ì, íì´ì§, ëì ê²ì","ì£¼ìë¥¼ ì ííë©´ ì ì²´ê° ë³µì¬ë©ëë¤.","ì¤ì ","QR ì½ë íì","íì´ë¡ë ì¢ë£","ê²ì ì ë ¬","ìì¤","ìì¤","ìë","Square","ê²ìì ì¤ííì¸ì","í¨ì¹ë¥¼ ì ì©íë ¤ë©´ ì´ ê²ìì ì¤ííì¸ì.","ìí","ìí ë° ë¡ê·¸","CheatRunner íì","ë¸ë¼ì§ìì íì","ì´ íë¡ì í¸ íì","íìì","ìì¤í","ê¸°ë³¸ ì¤ì  - íë§ì ì¹í¸ ìì§ ì¤ìì¹ìëë¤.","íë§","íë§ì ì¹í¸ ìì§","ë¨ì ìê°","íë ì´ ìê°","íì´í ID","í ê¸","TouchPad","í¸ë ì´ë","Triangle","ì","ìµì  ìí","ì¬ì©","ë²ì ","ê²ì ìì í ëê¸° (ms)","ê²½ê³  ë° ì¤ë¥","CheatRunnerì ëì ê¸°ë¡ìëë¤. ë¬¸ì ê° ìê²¼ì ë ì ì©í©ëë¤.","ì¹í¸ ìì","ââ ì´ë","ëª¨ë  ì¤ì ì´ ê¸°ë³¸ê°ì¼ë¡ ëìê°ëë¤. ì¹ í¬í¸ë ì ì§ë©ëë¤.","ë¤ë¥¸ ì¹í¸ë¥¼ ì ì©íë ì¤ìëë¤. ì ì í ë¤ì ìëíì¸ì.","ëª¨ë  ë²ì ","ê·¸ëë ì ì©","ìë ì íì´ ë³µìëììµëë¤.","ìë ë¡ëë¥¼ ì§ì ìµëë¤.","ìë ë¡ëë¥¼ ì ì¥íìµëë¤ ({n}). ê²ì ì¤í ì ì¼ì§ëë¤.","ë¹ ìë ë¡ëë¥¼ ì ì¥íìµëë¤.","ì¹í¸ #{n}","ì¹í¸ íì¼ì ì¶ê°íìµëë¤.","ì¹í¸ íì¼ì ì­ì íìµëë¤.","ì¹í¸ íì¼ì ì ííìµëë¤.","ì¤í ì¤ CheatRunnerê° ìëµíì§ ììµëë¤.","ì¹í¸ í´ëë¥¼ ë³µì¬íìµëë¤.","{name} íì¸ ì¤","ìë ë¡ë ì§ì°ê¸° ({n})","íì¼ì íëì© ë¹êµíê³  ë¤ì´ë¡ëíë ì¤","ë¤ë¥¸ ì¹í¸ì ì¶©ëí©ëë¤","ë³µì¬íì§ ëª»íìµëë¤. íì¤í¸ë¥¼ ì§ì  ì ííì¸ì.","ìë ë¡ëë¥¼ ì§ì°ì§ ëª»íìµëë¤.","ì§ë¨ ì ë³´ë¥¼ ë³µì¬íì§ ëª»íìµëë¤.","ì¹í¸ë¥¼ ëì§ ëª»íìµëë¤.","í¨ì¹ë¥¼ ë¶ë¬ì¤ì§ ëª»íìµëë¤.","ê²ì ëª©ë¡ì ë¶ë¬ì¤ì§ ëª»íìµëë¤.","ìë ë¡ëë¥¼ ì ì¥íì§ ëª»íìµëë¤.","ì ì¥íì§ ëª»íìµëë¤. ë¤ì ìëíì¸ì.","íì¼ì ì ííì§ ëª»íìµëë¤.","ë¤ì´ë¡ëë¥¼ ììíì§ ëª»íìµëë¤.","ììíì§ ëª»íìµëë¤.","ì¹í¸ë¥¼ ì ííì§ ëª»íìµëë¤.","ì­ì íì§ ëª»íìµëë¤.","íì¼ ì­ì ","{file} íì¼ì ì­ì í ê¹ì?","ì§ë¨ ì ë³´ë¥¼ ë³µì¬íìµëë¤. ì´ìì ë¶ì¬ë£ì¼ì¸ì.","{name} ë¤ì´ë¡ë ì¤","ìì§ êº¼ì§","ëª¨ë ìµì  ìíìëë¤","ë©ë´ í¼ì¹ê¸°","ì ì¥ì ì ì²´ë¥¼ í ë²ì ê°ì ¸ì¤ë ì¤","íì¼ v{a} / ê²ì v{b}","{name} ìë£","{name}ì(ë¥¼) ê°ì  ì ì©í ê¹ì?","ê²ìì ì¤ííìµëë¤.","ë¤ë¥¸ ê²ì ë²ì ì©ì´ë¼ ê²ìì´ ì¶©ëí  ì ììµëë¤. ë³µìíë©´ ìë ë°ì´í¸ë¡ ëìê°ëë¤.","ì¤ííì§ ëª»íìµëë¤.","ì¤ííì§ ëª»íìµëë¤: {msg}","ì¤í ìì²­ì ì¤í¨íìµëë¤. ê²ìì´ ìì§ ìì ì¤ì¼ ì ììµëë¤.","ì¤í ìê°ì´ ì´ê³¼ëììµëë¤. ê²ìì´ ììëëì§ íì¸íì¸ì.","{name} ì¤í ì¤...","ë¡ê·¸ë¥¼ ë³µì¬íìµëë¤.","maj0rê° {heart}ë¡ ë§ë¤ììµëë¤","v{v}ì ì¼ì¹","v{v} íì","ê²ì ìì","ê²ìê³¼ ì¼ì¹íë ê²ìì´ ììµëë¤.","ìì§ ì¹í¸ê° ìë ê²ìì´ ììµëë¤. ìì¤ìì ë°ìë³´ì¸ì.","ìì§ ë¡ê·¸ê° ììµëë¤.","ì¼ì¹íë ì¤ì´ ììµëë¤.","ì¤ë¹ ì ë¨","ìµê·¼ í­ëª©ì´ ììµëë¤. ê²ìì ì¤ííê±°ë í¸ë ì´ëë¥¼ ì´ì´ë³´ì¸ì.","ë³µì¬í  ë´ì©ì´ ììµëë¤.","ì¼ì§ í­ëª©ì´ ìììµëë¤.","í¸ë ì´ë ì´ê¸°: {name}","í¨ì¹ ìë ë¡ëë¥¼ ì§ì ìµëë¤.","í¨ì¹ ìë ë¡ëë¥¼ ì ì¥íìµëë¤.","QR ì½ëë¥¼ ì¬ì©í  ì ììµëë¤.","GitHubìì íì¼ ëª©ë¡ì ì½ë ì¤","ì¦ê²¨ì°¾ê¸°ìì ì ê±°","ì´ê¸°ííì§ ëª»íìµëë¤.","ì¤ì ì ì´ê¸°íí ê¹ì?","ì ì¥ë¨","ì¹í¸ íì¼ì ì ì¥íë ì¤","ì ì¥ ì¤...","ì´í PCìì ì CheatRunner.elfë¥¼ ë³´ë´ì¸ì.","ì¤ì ì ì´ê¸°ííìµëë¤.","ì¢ë£","CheatRunnerë¥¼ ì¢ë£í ê¹ì?","ìì¤ {i}/{n}","ë¨¼ì  ê²ìì ì¤ííì¸ì.","ê²ìì ìì§ ë¶ë¬ì¤ë ì¤ìëë¤.","ê²ìì´ ìì§ ìì ì¤ìëë¤. ëª ì´ í ë¤ì ìëíì¸ì.","ì§ê¸ì ê²ì ëª©ë¡ì ì¬ì©í  ì ììµëë¤.","ì§ê¸ì ì´ ì¹í¸ë¥¼ ì íí  ì ììµëë¤.","ì´ ì¹í¸ë ì´ì ì ê²ìì ì¶©ëìì¼°ìµëë¤. ë¤ì ìëíë ¤ë©´ íìë¥¼ ì§ì°ì¸ì.","ì´ íì¼ì ì¤ì¹ë ë²ì ê³¼ ë¤ë¥¸ ê²ì ë²ì ì©ìëë¤.","ì½ììì ì´ íì¼ì ì­ì í©ëë¤. ìì¤ìì ë¤ì ë°ì ì ììµëë¤.","ìì ìê° {t}","URLì ë³µì¬íìµëë¤.","{name} ìì¶ í´ì  ì¤","ì§ìëì§ ìë í¨ì¹","ìë¡ëíì§ ëª»íìµëë¤.",".mc4, .shn ëë .json íì¼ì ì¬ì©íì¸ì.","ë¤ë¥¸ ë²ì ì ì¬ì©í ê¹ì?","ê·¸ëë ì¬ì©","CheatRunnerê° ëìì¤ê¸°ë¥¼ ê¸°ë¤ë¦¬ë ì¤...","{label} ì£¼ìë¥¼ ë³µì¬íìµëë¤","ëª¨ë  ìì¤","íê·  {s}","ë°©ê¸ ì ","ì¹í¸ íì¼","v{v} ì¼ì¹","ì¹í¸ {n}ê°ë¥¼ ê»ìµëë¤.","ìë¡ ì¶ê°ëê±°ë ìë°ì´í¸ë íì¼ {n}ê°ë¥¼ ì ì¥íìµëë¤","{n}ê° íì´í","{n}ê° íì´í","{n}ì¼ ì ","{n}ìê° ì ","{n}ë¶ ì ","{page} - CheatRunner","{n}ê° ì¤ {a}ê° ì¼ì§","{h}ìê° {m}ë¶","{m}ë¶","{s}ì´","{m}ë¶ {s}ì´","ì ì: {a}","CheatRunnerì ì¤ì  ê²ì íìí©ëë¤.","ìì§ ì¹í¸ íì¼ì´ ììµëë¤. ìì¤ìì ì»¤ë®¤ëí° ì¹í¸ë¥¼ ë°ê±°ë, ê²ìì ì´ì´ íì¼ì ëì¼ì¸ì.","ì·¨ì","ê³ì","ë¤ì ì°ê²° ì¤...","ë³µì¬ë¨","ë³µì¬íìµëë¤.","ì ì ìì","ëí ìì","êº¼ì§","ê¶í","ì ì","ëë½","ì¤í¨íìµëë¤.","ì¤í ì¤","ë¤ì ìë","ê²ì","ì¹í¸ ê²ì","Xìì maj0r íë¡ì°","Blueskyìì maj0r íë¡ì°","ì ì: maj0r - ð§ð·","íë¡ì í¸ì ë³ì ë¨ê²¨ ì£¼ì¸ì! â­","í¬ë¦¼ì¨","ìë ë¡ë","ì´ê¸°í","ì´ ê²ìì ëí í¨ì¹ê° ììµëë¤.","XML í¨ì¹ë¥¼ /data/cheatrunner/patches/xml ì ë£ì¼ì¸ì","ìë°ì´í¸ ê°ë¥","ë²ì  {v}ì(ë¥¼) ì¬ì©í  ì ììµëë¤.","ì¨ë¼ì¸ìì ì¹í¸ ì°¾ìë³´ê¸°","ì¹í¸ê° ìë ëª¨ë  ê²ìì ê²ìíê³  ë´ ê²ìì´ ì§ìëëì§ íì¸íì¸ì.","ì¬ì´í¸ ì´ê¸°","ì»¤ë®¤ëí° í¨ì¹ ì ì¥ì","ê²ìì© í¨ì¹ ëª¨ììëë¤. ìë¬´ê²ë ë¤ì´ë¡ëëì§ ìì¼ë©° ì ì¥ìê° ì í­ìì ì´ë¦½ëë¤.","PS-Game-Patch ì´ê¸°","ìê° í¨ê³¼","âê°ìâë ë¸ë¬, ê·¸ë¦¼ì, ì ëë©ì´ìì ëëë¤. PS5 ë¸ë¼ì°ì ììë ê¸°ë³¸ê°ìëë¤.","ì ì²´","ê°ì","ë ë³´ê¸° ({n})","ì¶ì²","PS-Game-Patch ê³µì ë¦´ë¦¬ì¤ìëë¤. í¨ì¹ë /data/cheatrunner/patchesì ì ì¥ë©ëë¤.","í¨ì¹ ë¤ì´ë¡ë","í¨ì¹ íì¼ì ì ì¥íë ì¤","ì¼ë¶ í¨ì¹ë ê²ì ìì ì ì ì©í´ì¼ë§ ìëí©ëë¤. ìë ë¶ë¬ì¤ê¸°ë¥¼ ì ì¥íê³  ê²ìì ë¤ì ììíì¸ì.","ë¬¸ì  í´ê²°","ë¬¸ì ë¥¼ ì ê³ íê±°ë ëª¨ë  ì¤ì ì ê¸°ë³¸ê°ì¼ë¡ ëëë¦½ëë¤.","ì§ë¨ ì ë³´ë¥¼ ë³µì¬íê³  GitHub ì´ì íì´ì§ë¥¼ ì½ëë¤.","ì¹í¸, í¨ì¹, ìë ë¶ë¬ì¤ê¸° íë¡íì ë³ê²½ëì§ ììµëë¤.","ëª¨ë ì ììëë¤.","íì¸ì´ íìí í­ëª©ì´ ììµëë¤. ìë ì¸ë¶ ì ë³´ë¥¼ íì¸íì¸ì.","ì»´í¨í°ì .mc4, .shn ëë .json íì¼ìëë¤.","íì¼ ëë ìì¤ìì ì¶ê°íì¸ì","íë©´","íë©´, ì¹í¸ ìì§, ì»¨í¸ë¡¤ë¬ ìµììëë¤. ë³ê²½ ì¬í­ì ë°ë¡ ì ì¥ë©ëë¤.","ì¹í¸ íì¼","ì´ ê²ìì ì¹í¸ íì¼","CheatRunnerë ì´ ì½ìì ì¤ì¹ë ê²ìì ë³´ì¬ì¤ëë¤. ê²ìì ì¤ì¹í ë¤ ëª©ë¡ì ìë¡ê³ ì¹¨íì¸ì.","ì¼ê¸° ì ìë ì¹í¸ì í¨ì¹ë¥¼ ì ì©í  ì ììµëë¤.","íì¸ ì¤...","ë¤ë¥¸ íì¼ ì í","ë¶ë¬ì¬ ì ìì","ì¹í¸ íì¼ì ë¶ë¬ì¬ ì ììµëë¤","í¨ì¹ë¥¼ ë¶ë¬ì¬ ì ììµëë¤","ì¤ì ì ë¶ë¬ì¬ ì ììµëë¤","ê²ìì ë¶ë¬ì¬ ì ììµëë¤","ì¹í¸ì íì¬ ìíë¥¼ ì½ì ì ììµëë¤.","ìì¤ìì ì»¤ë®¤ëí° ì¹í¸ë¥¼ ë´ë ¤ë°ê±°ë ì»´í¨í°ì íì¼ì ì¶ê°íì¸ì.","ìì¤ìì ë´ë ¤ë°ê±°ë ìëì íì¼ì ëì¼ì¸ì.","ê²ìì© ì¹í¸ì í¨ì¹ë¥¼ ë´ë ¤ë°ìµëë¤. ë¤ë¥¸ íë©´ì ë³´ë ëììë ë¤ì´ë¡ëë ê³ìë©ëë¤.","ìì¤ìì ê³µì í¨ì¹ ëª¨ìì ë´ë ¤ë°ê±°ë XML í¨ì¹ë¥¼ /data/cheatrunner/patches/xml ì ë£ì¼ì¸ì.","ë¤ì´ë¡ë ì¤","ë¤ì´ë¡ë ì¤ {p}%","ë¤ì´ë¡ë","ìì§","ìµì  ì¤ì ë°ë¼ê°ë ì¤","ì¹í¸ ë°ê¸°","ìí","ë¼ì´ë¸ë¬ë¦¬","ì¹í¸ ìì§ê³¼ ë©ëª¨ë¦¬ í¨ì¹ì ì¤ìê° ìí, ê·¸ë¦¬ê³  CheatRunnerì ëì ê¸°ë¡ìëë¤.","ìì, ëìë§, ê·¸ë¦¬ê³  CheatRunnerë¥¼ ë§ëë ì¬ëë¤.","ì´ ê²ìì ì¹í¸ íì¼ì´ ìì§ ììµëë¤","\"{q}\"ì ì¼ì¹íë ì¹í¸ê° ììµëë¤.","ìì§ íì¼ ìì","ìì§ ê²ìì ì°¾ì§ ëª»íìµëë¤","ë¡ì»¬ íì¼ì ì°¾ì ì ììµëë¤","ì´ ê²ìì í¨ì¹ê° ììµëë¤","ì§ê¸ íë ì´ ì¤","ì¤ì  ì´ê¸°","ì½ë ëì ì¼ì ì¤ì§ë¨","íê²½ì¤ì ","âê°ìâë ëª¨ë  ì ëë©ì´ìì ì ê±°í©ëë¤. PS5 ë¸ë¼ì°ì ì ê¸°ë³¸ê°ìëë¤.","/data/cheatrunner/cheatsì ì ì¥ë©ëë¤. âì¬ì© ì¤âì¼ë¡ íìë íì¼ì í¸ë ì´ëê° ì½ìµëë¤.","ë¬¸ì ê° ìê¸°ë©´ ë³´ê³ ìë¥¼ ë³´ë´ ì£¼ì¸ì.","ì¤ííë©´ ì¹í¸ë¥¼ ì¬ì©í  ì ììµëë¤","ì´ ê²ìì ì¤ííë©´ ì¹í¸ë¥¼ ì¬ì©í  ì ììµëë¤.","ê¸°ì  ì¸ë¶ ì ë³´","ì¹í¸ ìì§ì´ êº¼ì ¸ ììµëë¤.","íì¬ ê°ì ì½ì ì ìì´ ìëª»ë ê°ì ë³´ì¬ ì£¼ì§ ìëë¡ ìë¬´ê²ë íìíì§ ììµëë¤.","ì¹í¸ì í¨ì¹ë¥¼ ì ì©íê¸° ìí ê¸°ë³¸ ì¤ì ìëë¤.","íì¼ì ìì§ë§ ì½ì ì ììµëë¤. ììëìê±°ë ìì§ ë³µì¬ ì¤ì¼ ì ììµëë¤.","íì¼ì ë¶ë¬ìì§ë§ ë¹ì´ ììµëë¤. ì´ ê²ìì ë¤ë¥¸ íì¼ì ì¬ì©í´ ë³´ì¸ì.","ì§ê¸ì ê²ì ëª©ë¡ì ë¶ë¬ì¬ ì ììµëë¤. ì½ìì´ ì¼ì ¸ ìëì§ íì¸íê³  ë¤ì ìëíì¸ì.","ì´ ê²ìì í¨ì¹ íì¼ì ì½ì ì ììµëë¤. ì ì í ë¤ì ìëíì¸ì.","ì¤ìì¹ ìíê° ìµì ì´ ìë ì ììµëë¤.","ì´ ëìë³´ëì íë§, í¨ê³¼, ì¸ì´ìëë¤.","ì´ ì¹í¸ íì¼ìë ì¹í¸ê° ììµëë¤","ìëìì ë¤ì ìëíì¸ì","ì¤ì ìì ì¼ì¸ì","ìí êµ¬ì­","ìµì ì¼ë¡ ì´ë","ì¬ì© ì¤ì¸ íì¼ ì­ì ","ì¹í¸ ìì¤","ê²ì íí°","ë´ ê²ì ë³´ê¸°","ì ìì","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"pl-PL":["Cheat spowodowaÅ ostatnio awariÄ gry.","O programie","Akcja","Akcje","Dodaj plik","Dodaj cheaty","Dodaj do ulubionych","Dodaje pozycjÄ â CheatRunner do menu opcji kaÅ¼dej gry oraz kombinacjÄ przyciskÃ³w otwierajÄcÄ trainer uruchomionej gry.","Zaawansowane","Wszystkie","Wszystkie wpÅaty trafiÄ na zakup PS5 z niskim firmware (1.xxâ4.xx) do badaÅ, rozwoju i przyszÅych ulepszeÅ.","Wszystkie pobierania nie powiodÅy siÄ","Wszystkie gry","Aktualizuj takÅ¼e zmienione pliki","Zastosowano","Zastosuj","Aplikacje","WybÃ³r automatyczny","Wstecz","PrzeglÄdaj bibliotekÄ","Przyciski","Buy Me a Coffee","Cheat","Silnik cheatÃ³w","CheatRunner - trainer cheatÃ³w na Twoje PS5!","Cheaty","Cheaty sÄ wstrzymywane przez ten czas po uruchomieniu gry.","Najpierw z cheatami","Wybierz sieÄ","Circle","WyczyÅÄ filtry","WyczyÅÄ oznaczenie","WyczyÅÄ wyszukiwanie","Zamknij","ZwiÅ","ZwiÅ menu","Schemat kolorÃ³w panelu.","SpoÅecznoÅÄ","Repozytoria cheatÃ³w spoÅecznoÅci","SkrÃ³t kontrolera i menu gry","Kopiuj","Kopiuj URL","Kopiuj wszystko","Kopiuj folder cheatÃ³w","Kopiuj link","Wykrywanie awarii","Ostatnio spowodowaÅ awariÄ gry","Cross","Krypto","Wsparcie w krypto","Ciemny","UsuÅ","WyÅÄcz wszystkie","DÃ³Å","Pobierz wszystkie ÅºrÃ³dÅa","Pobierz cheaty spoÅecznoÅci","Pobieranie nie powiodÅo siÄ","Pobieraj gotowe pliki cheatÃ³w do swoich gier. SÄ zapisywane w /data/cheatrunner/cheats, a pobieranie trwa w tle.","Pobierz wybrane","Pobrano","UpuÅÄ tutaj plik .mc4, .shn lub .json albo kliknij, aby wybraÄ","WÅÄcz skrÃ³t kontrolera","WÅÄcz skrÃ³t","Silnik wÅÄczony","Enter otwiera","Tylko bÅÄdy","DokÅadna","Niepowodzenia","Ulubione","Pliki","Filtruj","Filtruj logi","Oznacz cheat, jeÅli gra zamknie siÄ zaraz po jego wÅÄczeniu.","Dla uÅ¼ytkownikÃ³w. Przez uÅ¼ytkownikÃ³w.","WymuÅ zastosowanie","Gry","Gry z cheatami","Gry z patchami","OdstÄp miÄdzy cheatami (ms)","OgÃ³lne","Przytrzymane razem uruchamiajÄ skrÃ³t.","Czas przytrzymania (ms)","Przytrzymaj dwa przyciski razem, aby otworzyÄ trainer uruchomionej gry.","Kafelek na ekranie gÅÃ³wnym","Podpina siÄ pod interfejs systemu. DomyÅlnie wyÅÄczone - na niektÃ³rych firmware moÅ¼e zawiesiÄ ekran gÅÃ³wny.","Jak dÅugo trzymaÄ kombinacjÄ, zanim zadziaÅa.","Jak szczegÃ³Åowe informacje trafiajÄ do logu.","JeÅli CheatRunner Ci siÄ przydaÅ, rozwaÅ¼ wsparcie rozwoju projektu! ð«¶","W uÅ¼yciu","Instaluje kafelek CheatRunner na ekranie gÅÃ³wnym PS5 przy starcie.","DoÅÄcz do Discorda","Ko-Fi, kawa, krypto, PIX","JÄzyk","JÄzyk tego panelu.","Ostatnio grano","Lewo","Jasny","Stan silnika cheatÃ³w i patchowania pamiÄci.","Lokalne pliki cheatÃ³w","Poziom logÃ³w","Filtr poziomu logÃ³w","Logi","GÅÃ³wne","GÅÃ³wny przeÅÄcznik stosowania cheatÃ³w i patchy.","Korekta master code","MoÅ¼e nie pasowaÄ do tej wersji gry","Patchowanie pamiÄci","PÃ³Ånoc","Minimalny czas miÄdzy dwoma przeÅÄczeniami cheatÃ³w.","Nazwa A-Z","Nazwa Z-A","Brak jeszcze pliku cheatÃ³w dla tej gry.","Brak ulubionych. Dotknij gwiazdki przy grze, aby dodaÄ.","Nie uruchomiona","WÅ.","OtwÃ³rz na telefonie","OtwÃ³rz na telefonie (kod QR)","OtwÃ³rz panel na telefonie","OtwÃ³rz trainer","Opcje","Inna wersja","Strona","Patche","Graj","Kod QR","Kod QR adresu panelu","Ponownie stosuj wÅÄczone cheaty przy kaÅ¼dym uruchomieniu tej gry","Gotowe","Ostatnie","OdÅwieÅ¼","OdÅwieÅ¼ listÄ gier","Uruchom ponownie","ZgÅoÅ problem","Przeskanuj zainstalowane gry","PrzywrÃ³Ä domyÅlne","Automatycznie rozwiÄzuj cheaty zaleÅ¼ne od master code.","PrzywrÃ³Ä","Prawo","Uruchomiona","Teraz uruchomiona","Zapisz autoload","Zeskanuj aparatem w telefonie. Telefon musi byÄ w tej samej sieci Wi-Fi co PS5.","Szukaj","Szukaj wedÅug nazwy lub ID tytuÅu","Szukaj gier","Szukaj gier, stron i akcji","Wybierz adres, aby skopiowaÄ go w caÅoÅci.","Ustawienia","PokaÅ¼ kod QR","Zatrzymaj payload","Sortuj gry","Å¹rÃ³dÅo","Å¹rÃ³dÅa","PrÄdkoÅÄ","Square","Uruchom grÄ","Uruchom tÄ grÄ, aby zastosowaÄ patche.","Stan","Stan i logi","Wesprzyj CheatRunner","Wsparcie z Brazylii","Wesprzyj ten projekt","WspierajÄcy","System","NajwaÅ¼niejsze - motyw i przeÅÄcznik silnika cheatÃ³w.","Motyw","Motyw i silnik cheatÃ³w","PozostaÅo","Czas gry","ID tytuÅu","PrzeÅÄczniki","TouchPad","Trainer","Triangle","GÃ³ra","Aktualne","UÅ¼yj","Wersja","Oczekiwanie po starcie gry (ms)","OstrzeÅ¼enia i bÅÄdy","Co robiÅ CheatRunner - przydatne, gdy coÅ nie dziaÅa.","Z cheatami","ââ nawigacja","Wszystkie ustawienia wrÃ³cÄ do domyÅlnych. Port WWW zostanie zachowany.","Trwa stosowanie innego cheatu. SprÃ³buj ponownie za chwilÄ.","Dowolna wersja","Zastosuj mimo to","PrzywrÃ³cono wybÃ³r automatyczny.","Autoload wyczyszczony.","Zapisano autoload ({n}). WÅÄczÄ siÄ po uruchomieniu gry.","Zapisano pusty autoload.","Cheat nr {n}","Dodano plik cheatÃ³w.","UsuniÄto plik cheatÃ³w.","Wybrano plik cheatÃ³w.","CheatRunner przestaÅ odpowiadaÄ podczas uruchamiania.","Skopiowano folder cheatÃ³w.","Sprawdzanie: {name}","WyczyÅÄ autoload ({n})","PorÃ³wnywanie i pobieranie plikÃ³w po kolei","Koliduje z innym cheatem","Nie udaÅo siÄ skopiowaÄ. Zaznacz tekst rÄcznie.","Nie udaÅo siÄ wyczyÅciÄ autoloadu.","Nie udaÅo siÄ skopiowaÄ diagnostyki.","Nie udaÅo siÄ wyÅÄczyÄ cheatÃ³w.","Nie udaÅo siÄ wczytaÄ patchy.","Nie udaÅo siÄ wczytaÄ listy gier.","Nie udaÅo siÄ zapisaÄ autoloadu.","Nie udaÅo siÄ zapisaÄ. SprÃ³buj ponownie.","Nie udaÅo siÄ wybraÄ pliku.","Nie udaÅo siÄ rozpoczÄÄ pobierania.","Nie udaÅo siÄ uruchomiÄ.","Nie udaÅo siÄ przeÅÄczyÄ cheatu.","Nie udaÅo siÄ usunÄÄ.","UsuÅ plik","UsunÄÄ {file}?","Skopiowano diagnostykÄ. Wklej jÄ w zgÅoszeniu.","Pobieranie: {name}","Silnik wyÅÄczony","Wszystko jest juÅ¼ aktualne","RozwiÅ menu","Pobieranie caÅego repozytorium naraz","Plik v{a} / gra v{b}","ZakoÅczono: {name}","WymusiÄ zastosowanie {name}?","Gra uruchomiona.","Jest przeznaczony dla innej wersji gry i moÅ¼e jÄ zawiesiÄ. PrzywrÃ³Ä cofa oryginalne bajty.","Uruchomienie nie powiodÅo siÄ.","Uruchomienie nie powiodÅo siÄ: {msg}","Å»Ädanie uruchomienia nie powiodÅo siÄ. Gra moÅ¼e siÄ jeszcze uruchamiaÄ.","Przekroczono czas uruchamiania. SprawdÅº, czy gra siÄ uruchomiÅa.","Uruchamianie: {name}...","Skopiowano logi.","Stworzone z {heart} przez maj0r","Pasuje do v{v}","Wymaga v{v}","Brak gry","Å»adna gra nie pasuje do wyszukiwania.","Brak gier z cheatami. Pobierz je w Å¹rÃ³dÅach.","Brak logÃ³w.","Brak pasujÄcych linii.","Niegotowe","Brak ostatnich. Uruchom grÄ lub otwÃ³rz jej trainer.","Nie ma nic do skopiowania.","Nic nie byÅo wÅÄczone.","OtwÃ³rz trainer: {name}","Autoload patchy wyczyszczony.","Zapisano autoload patchy.","Kod QR jest niedostÄpny.","Odczyt listy plikÃ³w z GitHub","UsuÅ z ulubionych","Nie udaÅo siÄ zresetowaÄ.","ZresetowaÄ ustawienia?","Zapisano","Zapisywanie plikÃ³w cheatÃ³w","Zapisywanie...","NastÄpnie wyÅlij nowy CheatRunner.elf z komputera.","Zresetowano ustawienia.","Zamknij","ZatrzymaÄ CheatRunner?","Å¹rÃ³dÅo {i} z {n}","Najpierw uruchom grÄ.","Gra wciÄÅ¼ siÄ wczytuje.","Gra wciÄÅ¼ siÄ uruchamia. SprÃ³buj ponownie za kilka sekund.","Lista gier jest teraz niedostÄpna.","Tego cheatu nie moÅ¼na teraz przeÅÄczyÄ.","Ten cheat juÅ¼ wczeÅniej zawiesiÅ grÄ. WyczyÅÄ oznaczenie, aby sprÃ³bowaÄ ponownie.","Ten plik jest dla innej wersji gry niÅ¼ zainstalowana.","Spowoduje to usuniÄcie pliku z konsoli. MoÅ¼esz pobraÄ go ponownie ze Å¹rÃ³deÅ.","ZajÄÅo {t}","Skopiowano URL.","Rozpakowywanie: {name}","NieobsÅugiwany patch","PrzesyÅanie nie powiodÅo siÄ.","UÅ¼yj pliku .mc4, .shn lub .json.","UÅ¼yÄ innej wersji?","UÅ¼yj mimo to","Oczekiwanie na powrÃ³t CheatRunner...","Skopiowano adres {label}","wszystkie ÅºrÃ³dÅa","År. {s}","przed chwilÄ","plik cheatÃ³w","v{v} pasuje","WyÅÄczono cheatÃ³w: {n}.","Zapisano nowych lub zaktualizowanych plikÃ³w: {n}","Gier: {n}","Gier: {n}","{n} dni temu","{n} godz. temu","{n} min temu","{page} - CheatRunner","WÅÄczono {a} z {n}","{h} godz. {m} min","{m} min","{s} s","{m} min {s} s","autor: {a}","Witaj w CheatRunner.","Brak plikÃ³w cheatÃ³w. Pobierz cheaty spoÅecznoÅci ze Å¹rÃ³deÅ albo otwÃ³rz grÄ i upuÅÄ plik.","Anuluj","Kontynuuj","Ponowne ÅÄczenie...","Skopiowano","Skopiowano.","Nieznane","Okno dialogowe","WyÅ.","Uprawnienia","OK","Brak","Nie udaÅo siÄ.","Uruchamianie","SprÃ³buj ponownie","Gra","Szukaj cheatÃ³w","Obserwuj maj0r na X","Obserwuj maj0r na Bluesky","Autor: maj0r - ð§ð·","Zostaw gwiazdkÄ projektowi! â­","Karmazyn","Autoload","Resetuj","Brak patchy dla tej gry.","UmieÅÄ swoje patche XML w /data/cheatrunner/patches/xml","DostÄpna aktualizacja","DostÄpna jest wersja {v}.","PrzeglÄdaj cheaty online","Wyszukaj wszystkie gry z cheatami i sprawdÅº, czy Twoja jest objÄta.","OtwÃ³rz stronÄ","Repozytoria patchy spoÅecznoÅci","Gotowe patche do Twoich gier. Nic nie jest pobierane â repozytorium otworzy siÄ w nowej karcie.","OtwÃ³rz PS-Game-Patch","Efekty wizualne","Ograniczone wyÅÄczajÄ rozmycie, cienie i animacje. W przeglÄdarce PS5 to ustawienie domyÅlne.","PeÅne","Ograniczone","PokaÅ¼ wiÄcej ({n})","Zalecane","Oficjalne wydanie PS-Game-Patch. Åatki sÄ zapisywane w /data/cheatrunner/patches.","Pobierz Åatki","Zapisywanie plikÃ³w Åatek","NiektÃ³re Åatki dziaÅajÄ tylko po zastosowaniu przy uruchomieniu gry: zapisz autoÅadowanie i uruchom grÄ ponownie.","RozwiÄzywanie problemÃ³w","ZgÅoÅ problem albo przywrÃ³Ä wszystkie ustawienia domyÅlne.","Kopiuje dane diagnostyczne i otwiera stronÄ zgÅoszeÅ na GitHubie.","Cheaty, Åatki i profile autoÅadowania pozostajÄ nietkniÄte.","Wszystko wyglÄda dobrze.","CoÅ wymaga uwagi - sprawdÅº szczegÃ³Åy poniÅ¼ej.","Plik .mc4, .shn lub .json z Twojego komputera.","Dodaj plik w Pliki lub Å¹rÃ³dÅa","WyglÄd","WyglÄd, silnik cheatÃ³w i kontroler. Zmiany zapisujÄ siÄ od razu.","Plik cheatÃ³w","Pliki cheatÃ³w dla tej gry","CheatRunner pokazuje gry zainstalowane na tej konsoli. Zainstaluj grÄ i odÅwieÅ¼ listÄ.","Cheaty i patche nie zadziaÅajÄ, dopÃ³ki nie zostanie wÅÄczony.","Sprawdzanie...","Wybierz inny plik","Nie udaÅo siÄ wczytaÄ","Nie udaÅo siÄ wczytaÄ pliku cheatÃ³w","Nie udaÅo siÄ wczytaÄ patchy","Nie udaÅo siÄ wczytaÄ ustawieÅ","Nie udaÅo siÄ wczytaÄ Twoich gier","Nie udaÅo siÄ odczytaÄ bieÅ¼Äcego stanu cheatÃ³w.","Pobierz cheaty spoÅecznoÅci w Å¹rÃ³dÅach albo dodaj plik z komputera.","Pobierz go w Å¹rÃ³dÅach albo upuÅÄ plik poniÅ¼ej.","Pobieraj gotowe cheaty i patche do swoich gier. Pobieranie trwa w tle podczas przeglÄdania.","Pobierz oficjalny zestaw patchy w Å¹rÃ³dÅach albo umieÅÄ wÅasne patche XML w /data/cheatrunner/patches/xml.","Pobieranie","Pobieranie {p}%","Pobieranie","Silnik","Åledzenie najnowszych linii","Pobierz cheaty","Stan","Biblioteka","Stan silnika cheatÃ³w i patchowania pamiÄci na Å¼ywo oraz to, co robiÅ CheatRunner.","NowoÅci, pomoc i ludzie stojÄcy za CheatRunnerem.","Ta gra nie ma jeszcze pliku cheatÃ³w","Brak cheatÃ³w pasujÄcych do â{q}â.","Brak pliku","Nie znaleziono jeszcze gier","Nie znaleziono pliku lokalnego","Brak patchy dla tej gry","Teraz uruchomiona","OtwÃ³rz ustawienia","Wstrzymano na czas czytania","Preferencje","Tryb ograniczony usuwa wszystkie animacje. To ustawienie domyÅlne w przeglÄdarce PS5.","Zapisane w /data/cheatrunner/cheats. Trainer czyta plik oznaczony jako W uÅ¼yciu.","WyÅlij zgÅoszenie, gdy coÅ nie dziaÅa.","Uruchom, aby uÅ¼ywaÄ cheatÃ³w","Uruchom tÄ grÄ, aby uÅ¼ywaÄ jej cheatÃ³w.","SzczegÃ³Åy techniczne","Silnik cheatÃ³w jest wyÅÄczony.","Nie udaÅo siÄ odczytaÄ bieÅ¼Äcych wartoÅci, wiÄc nic nie pokazujemy, Å¼eby nie wyÅwietlaÄ bÅÄdnych.","Podstawy stosowania cheatÃ³w i patchy.","Plik istnieje, ale nie udaÅo siÄ go odczytaÄ. MoÅ¼e byÄ uszkodzony lub wciÄÅ¼ siÄ kopiuje.","Plik wczytano, ale jest pusty. SprÃ³buj innego pliku dla tej gry.","Lista gier jest teraz niedostÄpna. SprawdÅº, czy konsola jest wÅÄczona, i sprÃ³buj ponownie.","Nie udaÅo siÄ odczytaÄ plikÃ³w patchy tej gry. SprÃ³buj ponownie za chwilÄ.","PrzeÅÄczniki mogÄ byÄ nieaktualne.","Motyw, efekty i jÄzyk tego panelu.","Ten plik nie zawiera cheatÃ³w","SprÃ³buj ponownie poniÅ¼ej","WÅÄcz w Ustawieniach","Strefa zagroÅ¼enia","PrzejdÅº do najnowszych","UsuÅ uÅ¼ywany plik","Å¹rÃ³dÅo cheatÃ³w","Filtruj gry","Zobacz swoje gry","Autorzy","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"pt-BR":["Um cheat travou o jogo da Ãºltima vez.","Sobre","AÃ§Ã£o","AÃ§Ãµes","Adicionar um arquivo","Adicionar cheats","Adicionar aos favoritos","Adiciona uma entrada â CheatRunner ao menu de opÃ§Ãµes de cada jogo e uma combinaÃ§Ã£o de botÃµes que abre o trainer do jogo em execuÃ§Ã£o.","AvanÃ§ado","Todos","Todas as contribuiÃ§Ãµes irÃ£o para o nosso objetivo de adquirir um PS5 de firmware baixo (1.xxâ4.xx) para pesquisa, desenvolvimento e futuras atualizaÃ§Ãµes.","Todos os downloads falharam","Todos os jogos","Atualizar tambÃ©m os arquivos que mudaram","Aplicado","Aplicar","Apps","SeleÃ§Ã£o automÃ¡tica","Voltar","Navegue pela sua biblioteca","BotÃµes","Buy Me a Coffee","Cheat","Motor de cheats","CheatRunner - Cheat Trainer para o seu PS5!","Cheats","Os cheats sÃ£o adiados por este tempo apÃ³s o jogo iniciar.","Com cheats primeiro","Escolha uma rede","Circle","Limpar filtros","Limpar sinalizaÃ§Ã£o","Limpar busca","Fechar","Recolher","Recolher menu","Esquema de cores deste painel.","Comunidade","RepositÃ³rios de cheats da comunidade","Atalho do controle e menu do jogo","Copiar","Copiar URL","Copiar tudo","Copiar pasta de cheats","Copiar link","DetecÃ§Ã£o de falhas","Travou o jogo da Ãºltima vez","Cross","Cripto","Apoio com cripto","Escuro","Excluir","Desativar todos","Baixo","Baixar todas as fontes","Baixe cheats da comunidade","Falha no download","Baixe arquivos de cheats prontos para seus jogos. Eles sÃ£o salvos em /data/cheatrunner/cheats e os downloads continuam em segundo plano.","Baixar selecionado","Baixado","Solte um arquivo .mc4, .shn ou .json aqui, ou clique para procurar","Ativar atalho do controle","Ativar atalho","Motor ativo","Enter abre","Somente erros","Exata","Falhou","Favoritos","Arquivos","Filtrar","Filtrar logs","Sinalizar um cheat se o jogo fechar logo apÃ³s ele ser ativado.","Para usuÃ¡rios. Feito por usuÃ¡rios.","ForÃ§ar aplicaÃ§Ã£o","Jogos","Jogos com cheats","Jogos com patches","Intervalo entre cheats (ms)","Geral","Seguradas juntas para acionar o atalho.","Tempo de pressÃ£o (ms)","Segure dois botÃµes juntos para abrir o trainer do jogo em execuÃ§Ã£o.","Atalho na tela inicial","Conecta-se Ã  interface do sistema. Desativado por padrÃ£o - em alguns firmwares isso pode travar a tela inicial.","Quanto tempo segurar a combinaÃ§Ã£o antes de acionar.","Quanto detalhe Ã© gravado no log.","Se o CheatRunner foi Ãºtil para vocÃª, considere apoiar o desenvolvimento do projeto! ð«¶","Em uso","Instala o tile do CheatRunner na tela inicial do PS5 ao iniciar.","Entrar no Discord","Ko-Fi, cafÃ©, cripto, PIX","Idioma","Idioma deste painel.","Ãltimo jogo","Esquerda","Claro","Estado do motor de cheats e da escrita na memÃ³ria.","Arquivos de cheat locais","NÃ­vel de log","Filtro de nÃ­vel de log","Logs","Principal","Chave geral para aplicar cheats e patches.","CorreÃ§Ã£o de master code","Pode nÃ£o corresponder a esta versÃ£o do jogo","Patch de memÃ³ria","Meia-noite","Tempo mÃ­nimo entre duas ativaÃ§Ãµes de cheat.","Nome A-Z","Nome Z-A","Ainda nÃ£o hÃ¡ arquivo de cheat para este jogo.","Nenhum favorito ainda. Toque na estrela de um jogo para adicionar.","NÃ£o estÃ¡ em execuÃ§Ã£o","Ligado","Abrir no celular","Abrir no celular (QR code)","Abra o painel no seu celular","Abrir trainer","OpÃ§Ãµes","Outra versÃ£o","PÃ¡gina","Patches","Jogar","QR code","QR code do endereÃ§o do painel","Reaplicar os cheats ATIVADOS toda vez que este jogo iniciar","Pronto","Recentes","Atualizar","Atualizar lista de jogos","Reiniciar","Relatar um problema","Verificar jogos instalados novamente","Restaurar padrÃµes","Resolver automaticamente cheats dependentes de master code.","Restaurar","Direita","Em execuÃ§Ã£o","Em execuÃ§Ã£o agora","Salvar autoload","Escaneie com a cÃ¢mera do celular. O celular precisa estar no mesmo Wi-Fi do PS5.","Pesquisar","Pesquisar por nome ou ID do tÃ­tulo","Pesquisar jogos","Pesquisar jogos, pÃ¡ginas e aÃ§Ãµes","Selecione um endereÃ§o para copiÃ¡-lo por completo.","ConfiguraÃ§Ãµes","Mostrar QR code","Encerrar o payload","Ordenar jogos","CÃ³digo-fonte","Fontes","Velocidade","Square","Inicie o jogo","Inicie este jogo para aplicar patches.","Status","Status e logs","Apoie o CheatRunner","Apoio do Brasil","Apoie este projeto","Apoiadores","Sistema","O essencial - tema e a chave do motor de cheats.","Tema","Tema e motor de cheats","Tempo restante","Tempo jogado","ID do tÃ­tulo","AtivaÃ§Ãµes","TouchPad","Trainer","Triangle","Cima","Atualizados","Usar","VersÃ£o","Espera apÃ³s iniciar o jogo (ms)","Avisos e erros","O que o CheatRunner tem feito - Ãºtil quando algo nÃ£o funciona.","Com cheats","ââ navegar","Todas as configuraÃ§Ãµes voltam ao padrÃ£o. A porta web Ã© mantida.","Outro cheat estÃ¡ sendo aplicado. Tente de novo em instantes.","Qualquer versÃ£o","Aplicar mesmo assim","SeleÃ§Ã£o automÃ¡tica restaurada.","Autoload removido.","Autoload salvo ({n}). Eles sÃ£o ativados quando o jogo inicia.","Autoload salvo vazio.","Cheat nÂº {n}","Arquivo de cheat adicionado.","Arquivo de cheat excluÃ­do.","Arquivo de cheat selecionado.","O CheatRunner parou de responder durante a inicializaÃ§Ã£o.","Pasta de cheats copiada.","Verificando {name}","Limpar autoload ({n})","Comparando e baixando arquivos um a um","Conflita com outro cheat","Falha ao copiar. Selecione o texto manualmente.","NÃ£o foi possÃ­vel limpar o autoload.","NÃ£o foi possÃ­vel copiar o diagnÃ³stico.","NÃ£o foi possÃ­vel desativar os cheats.","NÃ£o foi possÃ­vel carregar os patches.","NÃ£o foi possÃ­vel carregar a lista de jogos.","NÃ£o foi possÃ­vel salvar o autoload.","NÃ£o foi possÃ­vel salvar. Tente de novo.","NÃ£o foi possÃ­vel selecionar o arquivo.","NÃ£o foi possÃ­vel iniciar o download.","NÃ£o foi possÃ­vel iniciar.","NÃ£o foi possÃ­vel alternar o cheat.","Falha ao excluir.","Excluir arquivo","Excluir {file}?","DiagnÃ³stico copiado. Cole-o no relato do problema.","Baixando {name}","Motor desligado","Tudo jÃ¡ estÃ¡ atualizado","Expandir menu","Buscando o repositÃ³rio inteiro de uma vez","Arquivo v{a} / jogo v{b}","{name} concluÃ­do","ForÃ§ar aplicaÃ§Ã£o de {name}?","Jogo iniciado.","Ele Ã© para outra versÃ£o do jogo e pode travar o jogo. Restaurar devolve os bytes originais.","Falha ao iniciar.","Falha ao iniciar: {msg}","Falha na solicitaÃ§Ã£o de inÃ­cio. O jogo ainda pode estar iniciando.","Tempo esgotado ao iniciar. Verifique se o jogo abriu.","Iniciando {name}...","Logs copiados.","Feito com {heart} por maj0r","Corresponde Ã  v{v}","Requer a v{v}","Nenhum jogo","Nenhum jogo corresponde Ã  sua busca.","Nenhum jogo com cheats ainda. Baixe alguns em Fontes.","Ainda nÃ£o hÃ¡ logs.","Nenhuma linha correspondente.","NÃ£o pronto","Nada recente. Inicie um jogo ou abra o trainer dele.","Nada para copiar.","Nada estava ligado.","Abrir trainer: {name}","Autoload de patches removido.","Autoload de patches salvo.","QR code indisponÃ­vel.","Lendo a lista de arquivos do GitHub","Remover dos favoritos","Falha ao restaurar.","Restaurar configuraÃ§Ãµes?","Salvo","Salvando os arquivos de cheat","Salvando...","Depois, envie um novo CheatRunner.elf do seu PC.","ConfiguraÃ§Ãµes restauradas.","Encerrar","Encerrar o CheatRunner?","Fonte {i} de {n}","Inicie o jogo primeiro.","O jogo ainda estÃ¡ carregando.","O jogo ainda estÃ¡ iniciando. Tente de novo em alguns segundos.","A lista de jogos estÃ¡ indisponÃ­vel no momento.","Este cheat nÃ£o pode ser alternado agora.","Este cheat jÃ¡ travou o jogo antes. Limpe a sinalizaÃ§Ã£o para tentar de novo.","Este arquivo Ã© de uma versÃ£o do jogo diferente da instalada.","Isso remove o arquivo do console. VocÃª pode baixÃ¡-lo de novo em Fontes.","Levou {t}","URL copiada.","Descompactando {name}","Patch nÃ£o suportado","Falha no envio.","Use um arquivo .mc4, .shn ou .json.","Usar outra versÃ£o?","Usar mesmo assim","Aguardando o CheatRunner voltar...","EndereÃ§o {label} copiado","todas as fontes","mÃ©dia {s}","agora mesmo","o arquivo de cheat","v{v} corresponde","{n} cheat(s) desativado(s).","{n} arquivo(s) novo(s) ou atualizado(s) salvo(s)","{n} tÃ­tulo","{n} tÃ­tulos","hÃ¡ {n} d","hÃ¡ {n} h","hÃ¡ {n} min","{page} - CheatRunner","{a} de {n} ativos","{h} h {m} min","{m} min","{s} s","{m} min {s} s","por {a}","Bem-vindo ao CheatRunner.","Ainda nÃ£o hÃ¡ arquivos de cheat. Baixe cheats da comunidade em Fontes ou abra um jogo e solte um arquivo.","Cancelar","Continuar","Reconectando...","Copiado","Copiado.","Desconhecido","DiÃ¡logo","Desligado","PrivilÃ©gios","OK","Ausente","Falhou.","Iniciando","Tentar de novo","Jogo","Pesquisar cheats","Siga o maj0r no X","Siga o maj0r no Bluesky","Feito por maj0r - ð§ð·","Deixe uma estrela no projeto! â­","Carmesim","Autoload","Restaurar","NÃ£o hÃ¡ patches para este jogo.","Coloque seus patches XML em /data/cheatrunner/patches/xml","AtualizaÃ§Ã£o disponÃ­vel","A versÃ£o {v} estÃ¡ disponÃ­vel.","Procurar cheats online","Pesquise todos os jogos com cheats e veja se o seu estÃ¡ coberto.","Abrir site","RepositÃ³rios de patches da comunidade","Patches prontos para seus jogos. Nada Ã© baixado - o repositÃ³rio abre em uma nova aba.","Abrir PS-Game-Patch","Efeitos visuais","Reduzido desliga desfoque, sombras e animaÃ§Ãµes. Ã o padrÃ£o no navegador do PS5.","Completo","Reduzido","Mostrar mais ({n})","Recomendado","VersÃ£o oficial do PS-Game-Patch. Os patches sÃ£o salvos em /data/cheatrunner/patches.","Baixar patches","Salvando os arquivos de patches","Alguns patches sÃ³ funcionam se aplicados no inÃ­cio do jogo: salve o autoload e reinicie o jogo.","SoluÃ§Ã£o de problemas","Relate um problema ou volte todas as configuraÃ§Ãµes ao padrÃ£o.","Copia o diagnÃ³stico e abre a pÃ¡gina de issues no GitHub.","Cheats, patches e perfis de autoload nÃ£o sÃ£o alterados.","Tudo parece estar bem.","Algo precisa de atenÃ§Ã£o - veja os detalhes abaixo.","Um arquivo .mc4, .shn ou .json do seu computador.","Adicione um em Arquivos ou Fontes","AparÃªncia","AparÃªncia, motor de cheats e controle. As mudanÃ§as sÃ£o salvas na hora.","Arquivo de cheat","Arquivos de cheat deste jogo","O CheatRunner lista os jogos instalados neste console. Instale um e atualize a lista.","Cheats e patches nÃ£o podem ser aplicados atÃ© que ele seja ligado.","Verificando...","Escolher outro arquivo","NÃ£o foi possÃ­vel carregar","NÃ£o foi possÃ­vel carregar o arquivo de cheat","NÃ£o foi possÃ­vel carregar os patches","NÃ£o foi possÃ­vel carregar as configuraÃ§Ãµes","NÃ£o foi possÃ­vel carregar seus jogos","NÃ£o foi possÃ­vel ler o estado atual dos cheats.","Baixe cheats da comunidade em Fontes ou adicione um arquivo do seu computador.","Baixe um em Fontes ou solte um arquivo abaixo.","Baixe cheats e patches prontos para seus jogos. Os downloads continuam enquanto vocÃª navega.","Baixe o conjunto oficial de patches em Fontes ou coloque seus patches XML em /data/cheatrunner/patches/xml.","Baixando","Baixando {p}%","Downloads","Motor","Acompanhando as Ãºltimas linhas","Obter cheats","SaÃºde","Biblioteca","Estado em tempo real do motor de cheats e da ediÃ§Ã£o de memÃ³ria, e o que o CheatRunner tem feito.","Novidades, ajuda e as pessoas por trÃ¡s do CheatRunner.","Ainda nÃ£o hÃ¡ arquivo de cheat para este jogo","Nenhum cheat corresponde a \"{q}\".","Ainda sem arquivo","Nenhum jogo encontrado ainda","Nenhum arquivo local encontrado","Nenhum patch para este jogo","Jogando agora","Abrir ConfiguraÃ§Ãµes","Pausado enquanto vocÃª lÃª","PreferÃªncias","O modo reduzido remove todas as animaÃ§Ãµes. Ã o padrÃ£o no navegador do PS5.","Salvos em /data/cheatrunner/cheats. O arquivo marcado como Em uso Ã© o que o trainer lÃª.","Envie um relatÃ³rio quando algo nÃ£o funcionar.","Inicie para usar cheats","Inicie este jogo para usar os cheats dele.","Detalhes tÃ©cnicos","O motor de cheats estÃ¡ desligado.","NÃ£o foi possÃ­vel ler os valores atuais, entÃ£o nada Ã© exibido para nÃ£o mostrar valores errados.","O essencial para aplicar cheats e patches.","O arquivo existe, mas nÃ£o pÃ´de ser lido. Ele pode estar danificado ou ainda sendo copiado.","O arquivo foi carregado, mas estÃ¡ vazio. Tente outro arquivo para este jogo.","A lista de jogos estÃ¡ indisponÃ­vel agora. Verifique se o console estÃ¡ ligado e tente de novo.","Os arquivos de patch deste jogo nÃ£o puderam ser lidos. Tente de novo em instantes.","Os interruptores podem estar desatualizados.","Tema, efeitos e idioma deste painel.","Este arquivo de cheat nÃ£o tem cheats","Tente de novo abaixo","Ligue em ConfiguraÃ§Ãµes","Zona de perigo","Ir para o mais recente","Excluir o arquivo em uso","Fonte de cheats","Filtrar jogos","Ver seus jogos","CrÃ©ditos","Baixe conjuntos de patches da comunidade. Os patches sÃ£o salvos em /data/cheatrunner/patches.","Abrir repositÃ³rio","Fonte de patches"],"ru-RU":["Ð§Ð¸Ñ Ð² Ð¿ÑÐ¾ÑÐ»ÑÐ¹ ÑÐ°Ð· Ð²ÑÐ·Ð²Ð°Ð» Ð²ÑÐ»ÐµÑ Ð¸Ð³ÑÑ.","Ð Ð¿ÑÐ¾Ð³ÑÐ°Ð¼Ð¼Ðµ","ÐÐµÐ¹ÑÑÐ²Ð¸Ðµ","ÐÐµÐ¹ÑÑÐ²Ð¸Ñ","ÐÐ¾Ð±Ð°Ð²Ð¸ÑÑ ÑÐ°Ð¹Ð»","ÐÐ¾Ð±Ð°Ð²Ð¸ÑÑ ÑÐ¸ÑÑ","ÐÐ¾Ð±Ð°Ð²Ð¸ÑÑ Ð² Ð¸Ð·Ð±ÑÐ°Ð½Ð½Ð¾Ðµ","ÐÐ¾Ð±Ð°Ð²Ð»ÑÐµÑ Ð¿ÑÐ½ÐºÑ â CheatRunner Ð² Ð¼ÐµÐ½Ñ Ð¿Ð°ÑÐ°Ð¼ÐµÑÑÐ¾Ð² ÐºÐ°Ð¶Ð´Ð¾Ð¹ Ð¸Ð³ÑÑ Ð¸ ÐºÐ¾Ð¼Ð±Ð¸Ð½Ð°ÑÐ¸Ñ ÐºÐ½Ð¾Ð¿Ð¾Ðº, Ð¾ÑÐºÑÑÐ²Ð°ÑÑÑÑ ÑÑÐµÐ¹Ð½ÐµÑ Ð·Ð°Ð¿ÑÑÐµÐ½Ð½Ð¾Ð¹ Ð¸Ð³ÑÑ.","ÐÐ¾Ð¿Ð¾Ð»Ð½Ð¸ÑÐµÐ»ÑÐ½Ð¾","ÐÑÐµ","ÐÑÐµ Ð¿Ð¾Ð¶ÐµÑÑÐ²Ð¾Ð²Ð°Ð½Ð¸Ñ Ð¿Ð¾Ð¹Ð´ÑÑ Ð½Ð° Ð¿Ð¾ÐºÑÐ¿ÐºÑ PS5 Ñ ÑÐ°Ð½Ð½ÐµÐ¹ Ð¿ÑÐ¾ÑÐ¸Ð²ÐºÐ¾Ð¹ (1.xxâ4.xx) Ð´Ð»Ñ Ð¸ÑÑÐ»ÐµÐ´Ð¾Ð²Ð°Ð½Ð¸Ð¹, ÑÐ°Ð·ÑÐ°Ð±Ð¾ÑÐºÐ¸ Ð¸ Ð±ÑÐ´ÑÑÐ¸Ñ ÑÐ»ÑÑÑÐµÐ½Ð¸Ð¹.","ÐÑÐµ Ð·Ð°Ð³ÑÑÐ·ÐºÐ¸ Ð½Ðµ ÑÐ´Ð°Ð»Ð¸ÑÑ","ÐÑÐµ Ð¸Ð³ÑÑ","Ð¢Ð°ÐºÐ¶Ðµ Ð¾Ð±Ð½Ð¾Ð²Ð»ÑÑÑ Ð¸Ð·Ð¼ÐµÐ½ÑÐ½Ð½ÑÐµ ÑÐ°Ð¹Ð»Ñ","ÐÑÐ¸Ð¼ÐµÐ½ÐµÐ½Ð¾","ÐÑÐ¸Ð¼ÐµÐ½Ð¸ÑÑ","ÐÑÐ¸Ð»Ð¾Ð¶ÐµÐ½Ð¸Ñ","ÐÐ²ÑÐ¾Ð²ÑÐ±Ð¾Ñ","ÐÐ°Ð·Ð°Ð´","ÐÑÐ¾ÑÐ¼Ð¾ÑÑ Ð±Ð¸Ð±Ð»Ð¸Ð¾ÑÐµÐºÐ¸","ÐÐ½Ð¾Ð¿ÐºÐ¸","Buy Me a Coffee","Ð§Ð¸Ñ","ÐÐ²Ð¸Ð¶Ð¾Ðº ÑÐ¸ÑÐ¾Ð²","CheatRunner â ÑÐ¸Ñ-ÑÑÐµÐ¹Ð½ÐµÑ Ð´Ð»Ñ Ð²Ð°ÑÐµÐ¹ PS5!","Ð§Ð¸ÑÑ","Ð§Ð¸ÑÑ ÑÐ´ÐµÑÐ¶Ð¸Ð²Ð°ÑÑÑÑ ÑÑÐ¾ Ð²ÑÐµÐ¼Ñ Ð¿Ð¾ÑÐ»Ðµ Ð·Ð°Ð¿ÑÑÐºÐ° Ð¸Ð³ÑÑ.","Ð¡Ð½Ð°ÑÐ°Ð»Ð° Ñ ÑÐ¸ÑÐ°Ð¼Ð¸","ÐÑÐ±ÐµÑÐ¸ÑÐµ ÑÐµÑÑ","Circle","Ð¡Ð±ÑÐ¾ÑÐ¸ÑÑ ÑÐ¸Ð»ÑÑÑÑ","Ð¡Ð½ÑÑÑ Ð¾ÑÐ¼ÐµÑÐºÑ","ÐÑÐ¸ÑÑÐ¸ÑÑ Ð¿Ð¾Ð¸ÑÐº","ÐÐ°ÐºÑÑÑÑ","Ð¡Ð²ÐµÑÐ½ÑÑÑ","Ð¡Ð²ÐµÑÐ½ÑÑÑ Ð¼ÐµÐ½Ñ","Ð¦Ð²ÐµÑÐ¾Ð²Ð°Ñ ÑÑÐµÐ¼Ð° Ð¿Ð°Ð½ÐµÐ»Ð¸.","Ð¡Ð¾Ð¾Ð±ÑÐµÑÑÐ²Ð¾","Ð ÐµÐ¿Ð¾Ð·Ð¸ÑÐ¾ÑÐ¸Ð¸ ÑÐ¸ÑÐ¾Ð² ÑÐ¾Ð¾Ð±ÑÐµÑÑÐ²Ð°","ÐÐ¾Ð¼Ð±Ð¸Ð½Ð°ÑÐ¸Ñ Ð½Ð° Ð³ÐµÐ¹Ð¼Ð¿Ð°Ð´Ðµ Ð¸ Ð¼ÐµÐ½Ñ Ð¸Ð³ÑÑ","ÐÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ","ÐÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ URL","ÐÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ Ð²ÑÑ","ÐÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ Ð¿Ð°Ð¿ÐºÑ ÑÐ¸ÑÐ¾Ð²","ÐÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ ÑÑÑÐ»ÐºÑ","ÐÐ±Ð½Ð°ÑÑÐ¶ÐµÐ½Ð¸Ðµ Ð²ÑÐ»ÐµÑÐ¾Ð²","Ð Ð¿ÑÐ¾ÑÐ»ÑÐ¹ ÑÐ°Ð· Ð²ÑÐ·Ð²Ð°Ð» Ð²ÑÐ»ÐµÑ Ð¸Ð³ÑÑ","Cross","ÐÑÐ¸Ð¿ÑÐ¾","ÐÐ¾Ð´Ð´ÐµÑÐ¶ÐºÐ° ÐºÑÐ¸Ð¿ÑÐ¾Ð²Ð°Ð»ÑÑÐ¾Ð¹","Ð¢ÑÐ¼Ð½Ð°Ñ","Ð£Ð´Ð°Ð»Ð¸ÑÑ","ÐÑÐºÐ»ÑÑÐ¸ÑÑ Ð²ÑÐµ","ÐÐ½Ð¸Ð·","Ð¡ÐºÐ°ÑÐ°ÑÑ Ð²ÑÐµ Ð¸ÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸","Ð¡ÐºÐ°ÑÐ°Ð¹ÑÐµ ÑÐ¸ÑÑ ÑÐ¾Ð¾Ð±ÑÐµÑÑÐ²Ð°","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ ÑÐºÐ°ÑÐ°ÑÑ","Ð¡ÐºÐ°ÑÐ¸Ð²Ð°Ð¹ÑÐµ Ð³Ð¾ÑÐ¾Ð²ÑÐµ ÑÐ°Ð¹Ð»Ñ ÑÐ¸ÑÐ¾Ð² Ð´Ð»Ñ Ð²Ð°ÑÐ¸Ñ Ð¸Ð³Ñ. ÐÐ½Ð¸ ÑÐ¾ÑÑÐ°Ð½ÑÑÑÑÑ Ð² /data/cheatrunner/cheats, Ð° Ð·Ð°Ð³ÑÑÐ·ÐºÐ° Ð¿ÑÐ¾Ð´Ð¾Ð»Ð¶Ð°ÐµÑÑÑ Ð² ÑÐ¾Ð½Ðµ.","Ð¡ÐºÐ°ÑÐ°ÑÑ Ð²ÑÐ±ÑÐ°Ð½Ð½Ð¾Ðµ","Ð¡ÐºÐ°ÑÐ°Ð½Ð¾","ÐÐµÑÐµÑÐ°ÑÐ¸ÑÐµ ÑÑÐ´Ð° ÑÐ°Ð¹Ð» .mc4, .shn Ð¸Ð»Ð¸ .json Ð¸Ð»Ð¸ Ð½Ð°Ð¶Ð¼Ð¸ÑÐµ Ð´Ð»Ñ Ð²ÑÐ±Ð¾ÑÐ°","ÐÐºÐ»ÑÑÐ¸ÑÑ ÐºÐ¾Ð¼Ð±Ð¸Ð½Ð°ÑÐ¸Ñ Ð½Ð° Ð³ÐµÐ¹Ð¼Ð¿Ð°Ð´Ðµ","ÐÐºÐ»ÑÑÐ¸ÑÑ ÐºÐ¾Ð¼Ð±Ð¸Ð½Ð°ÑÐ¸Ñ","ÐÐ²Ð¸Ð¶Ð¾Ðº Ð²ÐºÐ».","Enter â Ð¾ÑÐºÑÑÑÑ","Ð¢Ð¾Ð»ÑÐºÐ¾ Ð¾ÑÐ¸Ð±ÐºÐ¸","Ð¢Ð¾ÑÐ½Ð°Ñ","ÐÑÐ¸Ð±ÐºÐ¸","ÐÐ·Ð±ÑÐ°Ð½Ð½Ð¾Ðµ","Ð¤Ð°Ð¹Ð»Ñ","Ð¤Ð¸Ð»ÑÑÑ","Ð¤Ð¸Ð»ÑÑÑ Ð¶ÑÑÐ½Ð°Ð»Ð°","ÐÐ¾Ð¼ÐµÑÐ°ÑÑ ÑÐ¸Ñ, ÐµÑÐ»Ð¸ Ð¸Ð³ÑÐ° Ð·Ð°ÐºÑÑÐ»Ð°ÑÑ ÑÑÐ°Ð·Ñ Ð¿Ð¾ÑÐ»Ðµ ÐµÐ³Ð¾ Ð²ÐºÐ»ÑÑÐµÐ½Ð¸Ñ.","ÐÐ»Ñ Ð¿Ð¾Ð»ÑÐ·Ð¾Ð²Ð°ÑÐµÐ»ÐµÐ¹. ÐÑ Ð¿Ð¾Ð»ÑÐ·Ð¾Ð²Ð°ÑÐµÐ»ÐµÐ¹.","ÐÑÐ¸Ð¼ÐµÐ½Ð¸ÑÑ Ð¿ÑÐ¸Ð½ÑÐ´Ð¸ÑÐµÐ»ÑÐ½Ð¾","ÐÐ³ÑÑ","ÐÐ³ÑÑ Ñ ÑÐ¸ÑÐ°Ð¼Ð¸","ÐÐ³ÑÑ Ñ Ð¿Ð°ÑÑÐ°Ð¼Ð¸","ÐÐ°ÑÐ·Ð° Ð¼ÐµÐ¶Ð´Ñ ÑÐ¸ÑÐ°Ð¼Ð¸ (Ð¼Ñ)","ÐÐ±ÑÐ¸Ðµ","Ð£Ð´ÐµÑÐ¶Ð¸Ð²Ð°ÑÑÑÑ Ð²Ð¼ÐµÑÑÐµ Ð´Ð»Ñ ÑÑÐ°Ð±Ð°ÑÑÐ²Ð°Ð½Ð¸Ñ ÐºÐ¾Ð¼Ð±Ð¸Ð½Ð°ÑÐ¸Ð¸.","ÐÑÐµÐ¼Ñ ÑÐ´ÐµÑÐ¶Ð°Ð½Ð¸Ñ (Ð¼Ñ)","Ð£Ð´ÐµÑÐ¶Ð¸Ð²Ð°Ð¹ÑÐµ Ð´Ð²Ðµ ÐºÐ½Ð¾Ð¿ÐºÐ¸ Ð²Ð¼ÐµÑÑÐµ, ÑÑÐ¾Ð±Ñ Ð¾ÑÐºÑÑÑÑ ÑÑÐµÐ¹Ð½ÐµÑ Ð·Ð°Ð¿ÑÑÐµÐ½Ð½Ð¾Ð¹ Ð¸Ð³ÑÑ.","ÐÐ»Ð¸ÑÐºÐ° Ð½Ð° Ð³Ð»Ð°Ð²Ð½Ð¾Ð¼ ÑÐºÑÐ°Ð½Ðµ","ÐÐ½ÐµÐ´ÑÑÐµÑÑÑ Ð² ÑÐ¸ÑÑÐµÐ¼Ð½ÑÐ¹ Ð¸Ð½ÑÐµÑÑÐµÐ¹Ñ. ÐÐ¾ ÑÐ¼Ð¾Ð»ÑÐ°Ð½Ð¸Ñ Ð²ÑÐºÐ»ÑÑÐµÐ½Ð¾: Ð½Ð° Ð½ÐµÐºÐ¾ÑÐ¾ÑÑÑ Ð¿ÑÐ¾ÑÐ¸Ð²ÐºÐ°Ñ Ð¼Ð¾Ð¶ÐµÑ Ð²ÑÐ·Ð²Ð°ÑÑ ÑÐ±Ð¾Ð¹ Ð³Ð»Ð°Ð²Ð½Ð¾Ð³Ð¾ ÑÐºÑÐ°Ð½Ð°.","Ð¡ÐºÐ¾Ð»ÑÐºÐ¾ ÑÐ´ÐµÑÐ¶Ð¸Ð²Ð°ÑÑ ÐºÐ¾Ð¼Ð±Ð¸Ð½Ð°ÑÐ¸Ñ Ð´Ð¾ ÑÑÐ°Ð±Ð°ÑÑÐ²Ð°Ð½Ð¸Ñ.","ÐÐ°ÑÐºÐ¾Ð»ÑÐºÐ¾ Ð¿Ð¾Ð´ÑÐ¾Ð±Ð½Ð¾ Ð¿Ð¸ÑÐµÑÑÑ Ð¶ÑÑÐ½Ð°Ð».","ÐÑÐ»Ð¸ CheatRunner Ð²Ð°Ð¼ Ð¿ÑÐ¸Ð³Ð¾Ð´Ð¸Ð»ÑÑ, Ð¿Ð¾Ð´Ð´ÐµÑÐ¶Ð¸ÑÐµ ÑÐ°Ð·Ð²Ð¸ÑÐ¸Ðµ Ð¿ÑÐ¾ÐµÐºÑÐ°! ð«¶","ÐÑÐ¿Ð¾Ð»ÑÐ·ÑÐµÑÑÑ","Ð£ÑÑÐ°Ð½Ð°Ð²Ð»Ð¸Ð²Ð°ÐµÑ Ð¿Ð»Ð¸ÑÐºÑ CheatRunner Ð½Ð° Ð³Ð»Ð°Ð²Ð½ÑÐ¹ ÑÐºÑÐ°Ð½ PS5 Ð¿ÑÐ¸ Ð·Ð°Ð¿ÑÑÐºÐµ.","ÐÑÐ¸ÑÐ¾ÐµÐ´Ð¸Ð½Ð¸ÑÑÑÑ Ðº Discord","Ko-Fi, ÐºÐ¾ÑÐµ, ÐºÑÐ¸Ð¿ÑÐ¾, PIX","Ð¯Ð·ÑÐº","Ð¯Ð·ÑÐº ÑÑÐ¾Ð¹ Ð¿Ð°Ð½ÐµÐ»Ð¸.","ÐÐ¾ÑÐ»ÐµÐ´Ð½Ð¸Ð¹ Ð·Ð°Ð¿ÑÑÐº","ÐÐ»ÐµÐ²Ð¾","Ð¡Ð²ÐµÑÐ»Ð°Ñ","Ð¡Ð¾ÑÑÐ¾ÑÐ½Ð¸Ðµ Ð´Ð²Ð¸Ð¶ÐºÐ° ÑÐ¸ÑÐ¾Ð² Ð¸ Ð·Ð°Ð¿Ð¸ÑÐ¸ Ð² Ð¿Ð°Ð¼ÑÑÑ.","ÐÐ¾ÐºÐ°Ð»ÑÐ½ÑÐµ ÑÐ°Ð¹Ð»Ñ ÑÐ¸ÑÐ¾Ð²","Ð£ÑÐ¾Ð²ÐµÐ½Ñ Ð¶ÑÑÐ½Ð°Ð»Ð°","Ð¤Ð¸Ð»ÑÑÑ ÑÑÐ¾Ð²Ð½Ñ Ð¶ÑÑÐ½Ð°Ð»Ð°","ÐÑÑÐ½Ð°Ð»","ÐÑÐ½Ð¾Ð²Ð½Ð¾Ðµ","ÐÐ»Ð°Ð²Ð½ÑÐ¹ Ð²ÑÐºÐ»ÑÑÐ°ÑÐµÐ»Ñ Ð¿ÑÐ¸Ð¼ÐµÐ½ÐµÐ½Ð¸Ñ ÑÐ¸ÑÐ¾Ð² Ð¸ Ð¿Ð°ÑÑÐµÐ¹.","ÐÑÐ¿ÑÐ°Ð²Ð»ÐµÐ½Ð¸Ðµ Ð¼Ð°ÑÑÐµÑ-ÐºÐ¾Ð´Ð°","ÐÐ¾Ð¶ÐµÑ Ð½Ðµ Ð¿Ð¾Ð´ÑÐ¾Ð´Ð¸ÑÑ Ðº ÑÑÐ¾Ð¹ Ð²ÐµÑÑÐ¸Ð¸ Ð¸Ð³ÑÑ","ÐÐ°ÑÑÐ¸Ð½Ð³ Ð¿Ð°Ð¼ÑÑÐ¸","ÐÐ¾Ð»Ð½Ð¾ÑÑ","ÐÐ¸Ð½Ð¸Ð¼Ð°Ð»ÑÐ½Ð°Ñ Ð¿Ð°ÑÐ·Ð° Ð¼ÐµÐ¶Ð´Ñ Ð´Ð²ÑÐ¼Ñ Ð¿ÐµÑÐµÐºÐ»ÑÑÐµÐ½Ð¸ÑÐ¼Ð¸ ÑÐ¸ÑÐ¾Ð².","ÐÐ°Ð·Ð²Ð°Ð½Ð¸Ðµ Ð-Ð¯","ÐÐ°Ð·Ð²Ð°Ð½Ð¸Ðµ Ð¯-Ð","ÐÐ»Ñ ÑÑÐ¾Ð¹ Ð¸Ð³ÑÑ Ð¿Ð¾ÐºÐ° Ð½ÐµÑ ÑÐ°Ð¹Ð»Ð° ÑÐ¸ÑÐ¾Ð².","ÐÐ·Ð±ÑÐ°Ð½Ð½Ð¾Ð³Ð¾ Ð¿Ð¾ÐºÐ° Ð½ÐµÑ. ÐÐ°Ð¶Ð¼Ð¸ÑÐµ Ð½Ð° Ð·Ð²ÐµÐ·Ð´Ñ Ñ Ð¸Ð³ÑÑ, ÑÑÐ¾Ð±Ñ Ð´Ð¾Ð±Ð°Ð²Ð¸ÑÑ ÐµÑ.","ÐÐµ Ð·Ð°Ð¿ÑÑÐµÐ½Ð°","ÐÐºÐ».","ÐÑÐºÑÑÑÑ Ð½Ð° ÑÐµÐ»ÐµÑÐ¾Ð½Ðµ","ÐÑÐºÑÑÑÑ Ð½Ð° ÑÐµÐ»ÐµÑÐ¾Ð½Ðµ (QR-ÐºÐ¾Ð´)","ÐÑÐºÑÐ¾Ð¹ÑÐµ Ð¿Ð°Ð½ÐµÐ»Ñ Ð½Ð° ÑÐµÐ»ÐµÑÐ¾Ð½Ðµ","ÐÑÐºÑÑÑÑ ÑÑÐµÐ¹Ð½ÐµÑ","ÐÐ°ÑÐ°Ð¼ÐµÑÑÑ","ÐÑÑÐ³Ð°Ñ Ð²ÐµÑÑÐ¸Ñ","Ð¡ÑÑÐ°Ð½Ð¸ÑÐ°","ÐÐ°ÑÑÐ¸","ÐÐ³ÑÐ°ÑÑ","QR-ÐºÐ¾Ð´","QR-ÐºÐ¾Ð´ Ð°Ð´ÑÐµÑÐ° Ð¿Ð°Ð½ÐµÐ»Ð¸","ÐÐ¾Ð²ÑÐ¾ÑÐ½Ð¾ Ð¿ÑÐ¸Ð¼ÐµÐ½ÑÑÑ Ð²ÐºÐ»ÑÑÑÐ½Ð½ÑÐµ ÑÐ¸ÑÑ Ð¿ÑÐ¸ ÐºÐ°Ð¶Ð´Ð¾Ð¼ Ð·Ð°Ð¿ÑÑÐºÐµ ÑÑÐ¾Ð¹ Ð¸Ð³ÑÑ","ÐÐ¾ÑÐ¾Ð²Ð¾","ÐÐµÐ´Ð°Ð²Ð½Ð¸Ðµ","ÐÐ±Ð½Ð¾Ð²Ð¸ÑÑ","ÐÐ±Ð½Ð¾Ð²Ð¸ÑÑ ÑÐ¿Ð¸ÑÐ¾Ðº Ð¸Ð³Ñ","ÐÐµÑÐµÐ·Ð°Ð¿ÑÑÑÐ¸ÑÑ","Ð¡Ð¾Ð¾Ð±ÑÐ¸ÑÑ Ð¾ Ð¿ÑÐ¾Ð±Ð»ÐµÐ¼Ðµ","ÐÐµÑÐµÑÐºÐ°Ð½Ð¸ÑÐ¾Ð²Ð°ÑÑ ÑÑÑÐ°Ð½Ð¾Ð²Ð»ÐµÐ½Ð½ÑÐµ Ð¸Ð³ÑÑ","Ð¡Ð±ÑÐ¾ÑÐ¸ÑÑ Ð½Ð°ÑÑÑÐ¾Ð¹ÐºÐ¸","ÐÐ²ÑÐ¾Ð¼Ð°ÑÐ¸ÑÐµÑÐºÐ¸ ÑÐ°Ð·ÑÐµÑÐ°ÑÑ ÑÐ¸ÑÑ, Ð·Ð°Ð²Ð¸ÑÑÑÐ¸Ðµ Ð¾Ñ Ð¼Ð°ÑÑÐµÑ-ÐºÐ¾Ð´Ð°.","ÐÐ¾ÑÑÑÐ°Ð½Ð¾Ð²Ð¸ÑÑ","ÐÐ¿ÑÐ°Ð²Ð¾","ÐÐ°Ð¿ÑÑÐµÐ½Ð°","ÐÐ°Ð¿ÑÑÐµÐ½Ð° ÑÐµÐ¹ÑÐ°Ñ","Ð¡Ð¾ÑÑÐ°Ð½Ð¸ÑÑ Ð°Ð²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÑ","ÐÑÑÐºÐ°Ð½Ð¸ÑÑÐ¹ÑÐµ ÐºÐ°Ð¼ÐµÑÐ¾Ð¹ ÑÐµÐ»ÐµÑÐ¾Ð½Ð°. Ð¢ÐµÐ»ÐµÑÐ¾Ð½ Ð´Ð¾Ð»Ð¶ÐµÐ½ Ð±ÑÑÑ Ð² Ð¾Ð´Ð½Ð¾Ð¹ ÑÐµÑÐ¸ Wi-Fi Ñ PS5.","ÐÐ¾Ð¸ÑÐº","ÐÐ¾Ð¸ÑÐº Ð¿Ð¾ Ð½Ð°Ð·Ð²Ð°Ð½Ð¸Ñ Ð¸Ð»Ð¸ ID","ÐÐ¾Ð¸ÑÐº Ð¸Ð³Ñ","ÐÐ¾Ð¸ÑÐº Ð¸Ð³Ñ, ÑÑÑÐ°Ð½Ð¸Ñ Ð¸ Ð´ÐµÐ¹ÑÑÐ²Ð¸Ð¹","ÐÑÐ±ÐµÑÐ¸ÑÐµ Ð°Ð´ÑÐµÑ, ÑÑÐ¾Ð±Ñ ÑÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ ÐµÐ³Ð¾ Ð¿Ð¾Ð»Ð½Ð¾ÑÑÑÑ.","ÐÐ°ÑÑÑÐ¾Ð¹ÐºÐ¸","ÐÐ¾ÐºÐ°Ð·Ð°ÑÑ QR-ÐºÐ¾Ð´","ÐÑÑÐ°Ð½Ð¾Ð²Ð¸ÑÑ payload","Ð¡Ð¾ÑÑÐ¸ÑÐ¾Ð²ÐºÐ° Ð¸Ð³Ñ","ÐÑÑÐ¾Ð´Ð½ÑÐ¹ ÐºÐ¾Ð´","ÐÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸","Ð¡ÐºÐ¾ÑÐ¾ÑÑÑ","Square","ÐÐ°Ð¿ÑÑÑÐ¸ÑÐµ Ð¸Ð³ÑÑ","ÐÐ°Ð¿ÑÑÑÐ¸ÑÐµ ÑÑÑ Ð¸Ð³ÑÑ, ÑÑÐ¾Ð±Ñ Ð¿ÑÐ¸Ð¼ÐµÐ½ÑÑÑ Ð¿Ð°ÑÑÐ¸.","Ð¡Ð¾ÑÑÐ¾ÑÐ½Ð¸Ðµ","Ð¡Ð¾ÑÑÐ¾ÑÐ½Ð¸Ðµ Ð¸ Ð¶ÑÑÐ½Ð°Ð»Ñ","ÐÐ¾Ð´Ð´ÐµÑÐ¶Ð°ÑÑ CheatRunner","ÐÐ¾Ð´Ð´ÐµÑÐ¶ÐºÐ° Ð¸Ð· ÐÑÐ°Ð·Ð¸Ð»Ð¸Ð¸","ÐÐ¾Ð´Ð´ÐµÑÐ¶Ð°ÑÑ Ð¿ÑÐ¾ÐµÐºÑ","Ð¡Ð¿Ð¾Ð½ÑÐ¾ÑÑ","Ð¡Ð¸ÑÑÐµÐ¼Ð°","ÐÐ»Ð°Ð²Ð½Ð¾Ðµ: ÑÐµÐ¼Ð° Ð¸ Ð²ÑÐºÐ»ÑÑÐ°ÑÐµÐ»Ñ Ð´Ð²Ð¸Ð¶ÐºÐ° ÑÐ¸ÑÐ¾Ð².","Ð¢ÐµÐ¼Ð°","Ð¢ÐµÐ¼Ð° Ð¸ Ð´Ð²Ð¸Ð¶Ð¾Ðº ÑÐ¸ÑÐ¾Ð²","ÐÑÑÐ°Ð»Ð¾ÑÑ","ÐÑÐµÐ¼Ñ Ð² Ð¸Ð³ÑÐµ","ID Ð¸Ð³ÑÑ","ÐÐµÑÐµÐºÐ»ÑÑÐ°ÑÐµÐ»Ð¸","TouchPad","Ð¢ÑÐµÐ¹Ð½ÐµÑ","Triangle","ÐÐ²ÐµÑÑ","ÐÐºÑÑÐ°Ð»ÑÐ½ÑÐµ","ÐÑÐ¿Ð¾Ð»ÑÐ·Ð¾Ð²Ð°ÑÑ","ÐÐµÑÑÐ¸Ñ","ÐÐ°Ð´ÐµÑÐ¶ÐºÐ° Ð¿Ð¾ÑÐ»Ðµ Ð·Ð°Ð¿ÑÑÐºÐ° Ð¸Ð³ÑÑ (Ð¼Ñ)","ÐÑÐµÐ´ÑÐ¿ÑÐµÐ¶Ð´ÐµÐ½Ð¸Ñ Ð¸ Ð¾ÑÐ¸Ð±ÐºÐ¸","Ð§ÑÐ¾ Ð´ÐµÐ»Ð°Ð» CheatRunner â Ð¿Ð¾Ð»ÐµÐ·Ð½Ð¾, ÐµÑÐ»Ð¸ ÑÑÐ¾-ÑÐ¾ Ð½Ðµ ÑÐ°Ð±Ð¾ÑÐ°ÐµÑ.","Ð¡ ÑÐ¸ÑÐ°Ð¼Ð¸","ââ Ð½Ð°Ð²Ð¸Ð³Ð°ÑÐ¸Ñ","ÐÑÐµ Ð½Ð°ÑÑÑÐ¾Ð¹ÐºÐ¸ Ð²ÐµÑÐ½ÑÑÑÑ Ðº Ð·Ð½Ð°ÑÐµÐ½Ð¸ÑÐ¼ Ð¿Ð¾ ÑÐ¼Ð¾Ð»ÑÐ°Ð½Ð¸Ñ. ÐÐµÐ±-Ð¿Ð¾ÑÑ ÑÐ¾ÑÑÐ°Ð½Ð¸ÑÑÑ.","ÐÑÐ¸Ð¼ÐµÐ½ÑÐµÑÑÑ Ð´ÑÑÐ³Ð¾Ð¹ ÑÐ¸Ñ. ÐÐ¾Ð²ÑÐ¾ÑÐ¸ÑÐµ ÑÑÑÑ Ð¿Ð¾Ð·Ð¶Ðµ.","ÐÑÐ±Ð°Ñ Ð²ÐµÑÑÐ¸Ñ","ÐÑÐ¸Ð¼ÐµÐ½Ð¸ÑÑ Ð²ÑÑ ÑÐ°Ð²Ð½Ð¾","ÐÐ²ÑÐ¾Ð²ÑÐ±Ð¾Ñ Ð²Ð¾ÑÑÑÐ°Ð½Ð¾Ð²Ð»ÐµÐ½.","ÐÐ²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÐ° Ð¾ÑÐ¸ÑÐµÐ½Ð°.","ÐÐ²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÐ° ÑÐ¾ÑÑÐ°Ð½ÐµÐ½Ð° ({n}). ÐÐ½Ð¸ Ð²ÐºÐ»ÑÑÐ°ÑÑÑ Ð¿ÑÐ¸ Ð·Ð°Ð¿ÑÑÐºÐµ Ð¸Ð³ÑÑ.","Ð¡Ð¾ÑÑÐ°Ð½ÐµÐ½Ð° Ð¿ÑÑÑÐ°Ñ Ð°Ð²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÐ°.","Ð§Ð¸Ñ â{n}","Ð¤Ð°Ð¹Ð» ÑÐ¸ÑÐ¾Ð² Ð´Ð¾Ð±Ð°Ð²Ð»ÐµÐ½.","Ð¤Ð°Ð¹Ð» ÑÐ¸ÑÐ¾Ð² ÑÐ´Ð°Ð»ÑÐ½.","Ð¤Ð°Ð¹Ð» ÑÐ¸ÑÐ¾Ð² Ð²ÑÐ±ÑÐ°Ð½.","CheatRunner Ð¿ÐµÑÐµÑÑÐ°Ð» Ð¾ÑÐ²ÐµÑÐ°ÑÑ Ð²Ð¾ Ð²ÑÐµÐ¼Ñ Ð·Ð°Ð¿ÑÑÐºÐ°.","ÐÐ°Ð¿ÐºÐ° ÑÐ¸ÑÐ¾Ð² ÑÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°Ð½Ð°.","ÐÑÐ¾Ð²ÐµÑÐºÐ°: {name}","ÐÑÐ¸ÑÑÐ¸ÑÑ Ð°Ð²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÑ ({n})","Ð¡ÑÐ°Ð²Ð½ÐµÐ½Ð¸Ðµ Ð¸ Ð·Ð°Ð³ÑÑÐ·ÐºÐ° ÑÐ°Ð¹Ð»Ð¾Ð² Ð¿Ð¾ Ð¾Ð´Ð½Ð¾Ð¼Ñ","ÐÐ¾Ð½ÑÐ»Ð¸ÐºÑÑÐµÑ Ñ Ð´ÑÑÐ³Ð¸Ð¼ ÑÐ¸ÑÐ¾Ð¼","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ ÑÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ. ÐÑÐ´ÐµÐ»Ð¸ÑÐµ ÑÐµÐºÑÑ Ð²ÑÑÑÐ½ÑÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð¾ÑÐ¸ÑÑÐ¸ÑÑ Ð°Ð²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ ÑÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ Ð´Ð¸Ð°Ð³Ð½Ð¾ÑÑÐ¸ÐºÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð²ÑÐºÐ»ÑÑÐ¸ÑÑ ÑÐ¸ÑÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð³ÑÑÐ·Ð¸ÑÑ Ð¿Ð°ÑÑÐ¸.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð³ÑÑÐ·Ð¸ÑÑ ÑÐ¿Ð¸ÑÐ¾Ðº Ð¸Ð³Ñ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ ÑÐ¾ÑÑÐ°Ð½Ð¸ÑÑ Ð°Ð²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ ÑÐ¾ÑÑÐ°Ð½Ð¸ÑÑ. ÐÐ¾Ð²ÑÐ¾ÑÐ¸ÑÐµ Ð¿Ð¾Ð¿ÑÑÐºÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð²ÑÐ±ÑÐ°ÑÑ ÑÐ°Ð¹Ð».","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð½Ð°ÑÐ°ÑÑ Ð·Ð°Ð³ÑÑÐ·ÐºÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð¿ÑÑÑÐ¸ÑÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð¿ÐµÑÐµÐºÐ»ÑÑÐ¸ÑÑ ÑÐ¸Ñ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ ÑÐ´Ð°Ð»Ð¸ÑÑ.","Ð£Ð´Ð°Ð»Ð¸ÑÑ ÑÐ°Ð¹Ð»","Ð£Ð´Ð°Ð»Ð¸ÑÑ {file}?","ÐÐ¸Ð°Ð³Ð½Ð¾ÑÑÐ¸ÐºÐ° ÑÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°Ð½Ð°. ÐÑÑÐ°Ð²ÑÑÐµ ÐµÑ Ð² Ð¾Ð±ÑÐ°ÑÐµÐ½Ð¸Ðµ.","ÐÐ°Ð³ÑÑÐ·ÐºÐ°: {name}","ÐÐ²Ð¸Ð¶Ð¾Ðº Ð²ÑÐºÐ».","ÐÑÑ ÑÐ¶Ðµ Ð°ÐºÑÑÐ°Ð»ÑÐ½Ð¾","Ð Ð°Ð·Ð²ÐµÑÐ½ÑÑÑ Ð¼ÐµÐ½Ñ","ÐÐ°Ð³ÑÑÐ·ÐºÐ° Ð²ÑÐµÐ³Ð¾ ÑÐµÐ¿Ð¾Ð·Ð¸ÑÐ¾ÑÐ¸Ñ ÑÐµÐ»Ð¸ÐºÐ¾Ð¼","Ð¤Ð°Ð¹Ð» v{a} / Ð¸Ð³ÑÐ° v{b}","ÐÐ¾ÑÐ¾Ð²Ð¾: {name}","ÐÑÐ¸Ð¼ÐµÐ½Ð¸ÑÑ {name} Ð¿ÑÐ¸Ð½ÑÐ´Ð¸ÑÐµÐ»ÑÐ½Ð¾?","ÐÐ³ÑÐ° Ð·Ð°Ð¿ÑÑÐµÐ½Ð°.","ÐÐ½ ÑÐ°ÑÑÑÐ¸ÑÐ°Ð½ Ð½Ð° Ð´ÑÑÐ³ÑÑ Ð²ÐµÑÑÐ¸Ñ Ð¸Ð³ÑÑ Ð¸ Ð¼Ð¾Ð¶ÐµÑ Ð²ÑÐ·Ð²Ð°ÑÑ Ð²ÑÐ»ÐµÑ. Â«ÐÐ¾ÑÑÑÐ°Ð½Ð¾Ð²Ð¸ÑÑÂ» Ð²Ð¾Ð·Ð²ÑÐ°ÑÐ°ÐµÑ Ð¸ÑÑÐ¾Ð´Ð½ÑÐµ Ð±Ð°Ð¹ÑÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð¿ÑÑÑÐ¸ÑÑ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð¿ÑÑÑÐ¸ÑÑ: {msg}","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð¾ÑÐ¿ÑÐ°Ð²Ð¸ÑÑ Ð·Ð°Ð¿ÑÐ¾Ñ Ð·Ð°Ð¿ÑÑÐºÐ°. ÐÐ³ÑÐ°, Ð²Ð¾Ð·Ð¼Ð¾Ð¶Ð½Ð¾, ÐµÑÑ Ð·Ð°Ð¿ÑÑÐºÐ°ÐµÑÑÑ.","ÐÑÐµÐ¼Ñ Ð·Ð°Ð¿ÑÑÐºÐ° Ð¸ÑÑÐµÐºÐ»Ð¾. ÐÑÐ¾Ð²ÐµÑÑÑÐµ, Ð·Ð°Ð¿ÑÑÑÐ¸Ð»Ð°ÑÑ Ð»Ð¸ Ð¸Ð³ÑÐ°.","ÐÐ°Ð¿ÑÑÐº: {name}...","ÐÑÑÐ½Ð°Ð»Ñ ÑÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°Ð½Ñ.","Ð¡Ð´ÐµÐ»Ð°Ð½Ð¾ Ñ {heart} â maj0r","Ð¡Ð¾Ð²Ð¿Ð°Ð´Ð°ÐµÑ Ñ v{v}","Ð¢ÑÐµÐ±ÑÐµÑÑÑ v{v}","ÐÐµÑ Ð¸Ð³ÑÑ","ÐÐ¸ Ð¾Ð´Ð½Ð° Ð¸Ð³ÑÐ° Ð½Ðµ Ð¿Ð¾Ð´ÑÐ¾Ð´Ð¸Ñ Ð¿Ð¾Ð´ Ð¿Ð¾Ð¸ÑÐº.","ÐÐ³Ñ Ñ ÑÐ¸ÑÐ°Ð¼Ð¸ Ð¿Ð¾ÐºÐ° Ð½ÐµÑ. ÐÐ°Ð³ÑÑÐ·Ð¸ÑÐµ Ð¸Ñ Ð² ÑÐ°Ð·Ð´ÐµÐ»Ðµ Â«ÐÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸Â».","ÐÑÑÐ½Ð°Ð»Ð¾Ð² Ð¿Ð¾ÐºÐ° Ð½ÐµÑ.","ÐÐ¾Ð´ÑÐ¾Ð´ÑÑÐ¸Ñ ÑÑÑÐ¾Ðº Ð½ÐµÑ.","ÐÐµ Ð³Ð¾ÑÐ¾Ð²Ð¾","ÐÐµÐ´Ð°Ð²Ð½Ð¸Ñ Ð½ÐµÑ. ÐÐ°Ð¿ÑÑÑÐ¸ÑÐµ Ð¸Ð³ÑÑ Ð¸Ð»Ð¸ Ð¾ÑÐºÑÐ¾Ð¹ÑÐµ ÐµÑ ÑÑÐµÐ¹Ð½ÐµÑ.","ÐÐµÑÐµÐ³Ð¾ ÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°ÑÑ.","ÐÐ¸ÑÐµÐ³Ð¾ Ð½Ðµ Ð±ÑÐ»Ð¾ Ð²ÐºÐ»ÑÑÐµÐ½Ð¾.","ÐÑÐºÑÑÑÑ ÑÑÐµÐ¹Ð½ÐµÑ: {name}","ÐÐ²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÐ° Ð¿Ð°ÑÑÐµÐ¹ Ð¾ÑÐ¸ÑÐµÐ½Ð°.","ÐÐ²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÐ° Ð¿Ð°ÑÑÐµÐ¹ ÑÐ¾ÑÑÐ°Ð½ÐµÐ½Ð°.","QR-ÐºÐ¾Ð´ Ð½ÐµÐ´Ð¾ÑÑÑÐ¿ÐµÐ½.","Ð§ÑÐµÐ½Ð¸Ðµ ÑÐ¿Ð¸ÑÐºÐ° ÑÐ°Ð¹Ð»Ð¾Ð² Ñ GitHub","Ð£Ð±ÑÐ°ÑÑ Ð¸Ð· Ð¸Ð·Ð±ÑÐ°Ð½Ð½Ð¾Ð³Ð¾","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ ÑÐ±ÑÐ¾ÑÐ¸ÑÑ.","Ð¡Ð±ÑÐ¾ÑÐ¸ÑÑ Ð½Ð°ÑÑÑÐ¾Ð¹ÐºÐ¸?","Ð¡Ð¾ÑÑÐ°Ð½ÐµÐ½Ð¾","Ð¡Ð¾ÑÑÐ°Ð½ÐµÐ½Ð¸Ðµ ÑÐ°Ð¹Ð»Ð¾Ð² ÑÐ¸ÑÐ¾Ð²","Ð¡Ð¾ÑÑÐ°Ð½ÐµÐ½Ð¸Ðµ...","ÐÐ°ÑÐµÐ¼ Ð¾ÑÐ¿ÑÐ°Ð²ÑÑÐµ Ð½Ð¾Ð²ÑÐ¹ CheatRunner.elf Ñ ÐºÐ¾Ð¼Ð¿ÑÑÑÐµÑÐ°.","ÐÐ°ÑÑÑÐ¾Ð¹ÐºÐ¸ ÑÐ±ÑÐ¾ÑÐµÐ½Ñ.","ÐÑÐºÐ»ÑÑÐ¸ÑÑ","ÐÑÐºÐ»ÑÑÐ¸ÑÑ CheatRunner?","ÐÑÑÐ¾ÑÐ½Ð¸Ðº {i} Ð¸Ð· {n}","Ð¡Ð½Ð°ÑÐ°Ð»Ð° Ð·Ð°Ð¿ÑÑÑÐ¸ÑÐµ Ð¸Ð³ÑÑ.","ÐÐ³ÑÐ° ÐµÑÑ Ð·Ð°Ð³ÑÑÐ¶Ð°ÐµÑÑÑ.","ÐÐ³ÑÐ° ÐµÑÑ Ð·Ð°Ð¿ÑÑÐºÐ°ÐµÑÑÑ. ÐÐ¾Ð²ÑÐ¾ÑÐ¸ÑÐµ ÑÐµÑÐµÐ· Ð½ÐµÑÐºÐ¾Ð»ÑÐºÐ¾ ÑÐµÐºÑÐ½Ð´.","Ð¡Ð¿Ð¸ÑÐ¾Ðº Ð¸Ð³Ñ ÑÐµÐ¹ÑÐ°Ñ Ð½ÐµÐ´Ð¾ÑÑÑÐ¿ÐµÐ½.","Ð­ÑÐ¾Ñ ÑÐ¸Ñ ÑÐµÐ¹ÑÐ°Ñ Ð½ÐµÐ»ÑÐ·Ñ Ð¿ÐµÑÐµÐºÐ»ÑÑÐ¸ÑÑ.","Ð­ÑÐ¾Ñ ÑÐ¸Ñ ÑÐ¶Ðµ Ð²ÑÐ·ÑÐ²Ð°Ð» Ð²ÑÐ»ÐµÑ Ð¸Ð³ÑÑ. Ð¡Ð½Ð¸Ð¼Ð¸ÑÐµ Ð¾ÑÐ¼ÐµÑÐºÑ, ÑÑÐ¾Ð±Ñ Ð¿Ð¾Ð²ÑÐ¾ÑÐ¸ÑÑ.","Ð­ÑÐ¾Ñ ÑÐ°Ð¹Ð» Ð´Ð»Ñ Ð´ÑÑÐ³Ð¾Ð¹ Ð²ÐµÑÑÐ¸Ð¸ Ð¸Ð³ÑÑ, Ð½Ðµ ÑÐ¾Ð¹, ÑÑÐ¾ ÑÑÑÐ°Ð½Ð¾Ð²Ð»ÐµÐ½Ð°.","Ð¤Ð°Ð¹Ð» Ð±ÑÐ´ÐµÑ ÑÐ´Ð°Ð»ÑÐ½ Ñ ÐºÐ¾Ð½ÑÐ¾Ð»Ð¸. ÐÐ³Ð¾ Ð¼Ð¾Ð¶Ð½Ð¾ ÑÐºÐ°ÑÐ°ÑÑ ÑÐ½Ð¾Ð²Ð° Ð² ÑÐ°Ð·Ð´ÐµÐ»Ðµ Â«ÐÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸Â».","ÐÐ°Ð½ÑÐ»Ð¾ {t}","URL ÑÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°Ð½.","Ð Ð°ÑÐ¿Ð°ÐºÐ¾Ð²ÐºÐ°: {name}","ÐÐ°ÑÑ Ð½Ðµ Ð¿Ð¾Ð´Ð´ÐµÑÐ¶Ð¸Ð²Ð°ÐµÑÑÑ","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð³ÑÑÐ·Ð¸ÑÑ ÑÐ°Ð¹Ð».","ÐÑÐ¿Ð¾Ð»ÑÐ·ÑÐ¹ÑÐµ ÑÐ°Ð¹Ð» .mc4, .shn Ð¸Ð»Ð¸ .json.","ÐÑÐ¿Ð¾Ð»ÑÐ·Ð¾Ð²Ð°ÑÑ Ð´ÑÑÐ³ÑÑ Ð²ÐµÑÑÐ¸Ñ?","ÐÑÑ ÑÐ°Ð²Ð½Ð¾ Ð¸ÑÐ¿Ð¾Ð»ÑÐ·Ð¾Ð²Ð°ÑÑ","ÐÐ¶Ð¸Ð´Ð°Ð½Ð¸Ðµ Ð²Ð¾Ð·Ð²ÑÐ°ÑÐµÐ½Ð¸Ñ CheatRunner...","ÐÐ´ÑÐµÑ {label} ÑÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°Ð½","Ð²ÑÐµ Ð¸ÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸","Ð² ÑÑÐµÐ´Ð½ÐµÐ¼ {s}","ÑÐ¾Ð»ÑÐºÐ¾ ÑÑÐ¾","ÑÐ°Ð¹Ð» ÑÐ¸ÑÐ¾Ð²","v{v} Ð¿Ð¾Ð´ÑÐ¾Ð´Ð¸Ñ","Ð§Ð¸ÑÐ¾Ð² Ð²ÑÐºÐ»ÑÑÐµÐ½Ð¾: {n}.","Ð¡Ð¾ÑÑÐ°Ð½ÐµÐ½Ð¾ Ð½Ð¾Ð²ÑÑ Ð¸Ð»Ð¸ Ð¾Ð±Ð½Ð¾Ð²Ð»ÑÐ½Ð½ÑÑ ÑÐ°Ð¹Ð»Ð¾Ð²: {n}","ÐÐ³Ñ: {n}","ÐÐ³Ñ: {n}","{n} Ð´Ð½. Ð½Ð°Ð·Ð°Ð´","{n} Ñ Ð½Ð°Ð·Ð°Ð´","{n} Ð¼Ð¸Ð½ Ð½Ð°Ð·Ð°Ð´","{page} - CheatRunner","ÐÐºÐ»ÑÑÐµÐ½Ð¾ {a} Ð¸Ð· {n}","{h} Ñ {m} Ð¼Ð¸Ð½","{m} Ð¼Ð¸Ð½","{s} Ñ","{m} Ð¼Ð¸Ð½ {s} Ñ","Ð°Ð²ÑÐ¾Ñ: {a}","ÐÐ¾Ð±ÑÐ¾ Ð¿Ð¾Ð¶Ð°Ð»Ð¾Ð²Ð°ÑÑ Ð² CheatRunner.","Ð¤Ð°Ð¹Ð»Ð¾Ð² ÑÐ¸ÑÐ¾Ð² Ð¿Ð¾ÐºÐ° Ð½ÐµÑ. Ð¡ÐºÐ°ÑÐ°Ð¹ÑÐµ ÑÐ¸ÑÑ ÑÐ¾Ð¾Ð±ÑÐµÑÑÐ²Ð° Ð² Â«ÐÑÑÐ¾ÑÐ½Ð¸ÐºÐ°ÑÂ» Ð¸Ð»Ð¸ Ð¾ÑÐºÑÐ¾Ð¹ÑÐµ Ð¸Ð³ÑÑ Ð¸ Ð¿ÐµÑÐµÑÐ°ÑÐ¸ÑÐµ ÑÐ°Ð¹Ð».","ÐÑÐ¼ÐµÐ½Ð°","ÐÑÐ¾Ð´Ð¾Ð»Ð¶Ð¸ÑÑ","ÐÐµÑÐµÐ¿Ð¾Ð´ÐºÐ»ÑÑÐµÐ½Ð¸Ðµ...","Ð¡ÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°Ð½Ð¾","Ð¡ÐºÐ¾Ð¿Ð¸ÑÐ¾Ð²Ð°Ð½Ð¾.","ÐÐµÐ¸Ð·Ð²ÐµÑÑÐ½Ð¾","ÐÐ¸Ð°Ð»Ð¾Ð³","ÐÑÐºÐ».","ÐÑÐ¸Ð²Ð¸Ð»ÐµÐ³Ð¸Ð¸","OK","ÐÑÑÑÑÑÑÐ²ÑÐµÑ","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ.","ÐÐ°Ð¿ÑÑÐº","ÐÐ¾Ð²ÑÐ¾ÑÐ¸ÑÑ","ÐÐ³ÑÐ°","ÐÐ¾Ð¸ÑÐº ÑÐ¸ÑÐ¾Ð²","ÐÐ¾Ð´Ð¿Ð¸ÑÐ¸ÑÐµÑÑ Ð½Ð° maj0r Ð² X","ÐÐ¾Ð´Ð¿Ð¸ÑÐ¸ÑÐµÑÑ Ð½Ð° maj0r Ð² Bluesky","ÐÐ²ÑÐ¾Ñ: maj0r - ð§ð·","ÐÐ¾ÑÑÐ°Ð²ÑÑÐµ Ð¿ÑÐ¾ÐµÐºÑÑ Ð·Ð²ÐµÐ·Ð´Ñ! â­","ÐÐ°Ð»Ð¸Ð½Ð¾Ð²Ð°Ñ","ÐÐ²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÐ°","Ð¡Ð±ÑÐ¾ÑÐ¸ÑÑ","ÐÐ»Ñ ÑÑÐ¾Ð¹ Ð¸Ð³ÑÑ Ð½ÐµÑ Ð¿Ð°ÑÑÐµÐ¹.","ÐÐ¾Ð¼ÐµÑÑÐ¸ÑÐµ ÑÐ²Ð¾Ð¸ XML-Ð¿Ð°ÑÑÐ¸ Ð² /data/cheatrunner/patches/xml","ÐÐ¾ÑÑÑÐ¿Ð½Ð¾ Ð¾Ð±Ð½Ð¾Ð²Ð»ÐµÐ½Ð¸Ðµ","ÐÐ¾ÑÑÑÐ¿Ð½Ð° Ð²ÐµÑÑÐ¸Ñ {v}.","ÐÑÐºÐ°ÑÑ ÑÐ¸ÑÑ Ð¾Ð½Ð»Ð°Ð¹Ð½","ÐÐ°Ð¹Ð´Ð¸ÑÐµ Ð²ÑÐµ Ð¸Ð³ÑÑ Ñ ÑÐ¸ÑÐ°Ð¼Ð¸ Ð¸ Ð¿ÑÐ¾Ð²ÐµÑÑÑÐµ, ÐµÑÑÑ Ð»Ð¸ Ð²Ð°ÑÐ°.","ÐÑÐºÑÑÑÑ ÑÐ°Ð¹Ñ","Ð ÐµÐ¿Ð¾Ð·Ð¸ÑÐ¾ÑÐ¸Ð¸ Ð¿Ð°ÑÑÐµÐ¹ ÑÐ¾Ð¾Ð±ÑÐµÑÑÐ²Ð°","ÐÐ¾ÑÐ¾Ð²ÑÐµ Ð¿Ð°ÑÑÐ¸ Ð´Ð»Ñ Ð²Ð°ÑÐ¸Ñ Ð¸Ð³Ñ. ÐÐ¸ÑÐµÐ³Ð¾ Ð½Ðµ Ð·Ð°Ð³ÑÑÐ¶Ð°ÐµÑÑÑ â ÑÐµÐ¿Ð¾Ð·Ð¸ÑÐ¾ÑÐ¸Ð¹ Ð¾ÑÐºÑÐ¾ÐµÑÑÑ Ð² Ð½Ð¾Ð²Ð¾Ð¹ Ð²ÐºÐ»Ð°Ð´ÐºÐµ.","ÐÑÐºÑÑÑÑ PS-Game-Patch","ÐÐ¸Ð·ÑÐ°Ð»ÑÐ½ÑÐµ ÑÑÑÐµÐºÑÑ","Ð ÑÐµÐ¶Ð¸Ð¼Ðµ Â«Ð¡Ð½Ð¸Ð¶ÐµÐ½Ð½ÑÐµÂ» Ð¾ÑÐºÐ»ÑÑÐ°ÑÑÑÑ ÑÐ°Ð·Ð¼ÑÑÐ¸Ðµ, ÑÐµÐ½Ð¸ Ð¸ Ð°Ð½Ð¸Ð¼Ð°ÑÐ¸Ð¸. Ð Ð±ÑÐ°ÑÐ·ÐµÑÐµ PS5 Ð¾Ð½ Ð²ÐºÐ»ÑÑÑÐ½ Ð¿Ð¾ ÑÐ¼Ð¾Ð»ÑÐ°Ð½Ð¸Ñ.","ÐÐ¾Ð»Ð½ÑÐµ","Ð¡Ð½Ð¸Ð¶ÐµÐ½Ð½ÑÐµ","ÐÐ¾ÐºÐ°Ð·Ð°ÑÑ ÐµÑÑ ({n})","Ð ÐµÐºÐ¾Ð¼ÐµÐ½Ð´ÑÐµÑÑÑ","ÐÑÐ¸ÑÐ¸Ð°Ð»ÑÐ½ÑÐ¹ ÑÐµÐ»Ð¸Ð· PS-Game-Patch. ÐÐ°ÑÑÐ¸ ÑÐ¾ÑÑÐ°Ð½ÑÑÑÑÑ Ð² /data/cheatrunner/patches.","Ð¡ÐºÐ°ÑÐ°ÑÑ Ð¿Ð°ÑÑÐ¸","Ð¡Ð¾ÑÑÐ°Ð½ÐµÐ½Ð¸Ðµ ÑÐ°Ð¹Ð»Ð¾Ð² Ð¿Ð°ÑÑÐµÐ¹","ÐÐµÐºÐ¾ÑÐ¾ÑÑÐµ Ð¿Ð°ÑÑÐ¸ ÑÐ°Ð±Ð¾ÑÐ°ÑÑ ÑÐ¾Ð»ÑÐºÐ¾ Ð¿ÑÐ¸ Ð¿ÑÐ¸Ð¼ÐµÐ½ÐµÐ½Ð¸Ð¸ Ð² Ð½Ð°ÑÐ°Ð»Ðµ Ð¸Ð³ÑÑ: ÑÐ¾ÑÑÐ°Ð½Ð¸ÑÐµ Ð°Ð²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÑ Ð¸ Ð¿ÐµÑÐµÐ·Ð°Ð¿ÑÑÑÐ¸ÑÐµ Ð¸Ð³ÑÑ.","Ð£ÑÑÑÐ°Ð½ÐµÐ½Ð¸Ðµ Ð½ÐµÐ¿Ð¾Ð»Ð°Ð´Ð¾Ðº","Ð¡Ð¾Ð¾Ð±ÑÐ¸ÑÐµ Ð¾ Ð¿ÑÐ¾Ð±Ð»ÐµÐ¼Ðµ Ð¸Ð»Ð¸ Ð²ÐµÑÐ½Ð¸ÑÐµ Ð²ÑÐµ Ð½Ð°ÑÑÑÐ¾Ð¹ÐºÐ¸ Ðº Ð·Ð½Ð°ÑÐµÐ½Ð¸ÑÐ¼ Ð¿Ð¾ ÑÐ¼Ð¾Ð»ÑÐ°Ð½Ð¸Ñ.","ÐÐ¾Ð¿Ð¸ÑÑÐµÑ Ð´Ð¸Ð°Ð³Ð½Ð¾ÑÑÐ¸ÐºÑ Ð¸ Ð¾ÑÐºÑÑÐ²Ð°ÐµÑ ÑÑÑÐ°Ð½Ð¸ÑÑ Ð¾Ð±ÑÐ°ÑÐµÐ½Ð¸Ð¹ Ð½Ð° GitHub.","Ð§Ð¸ÑÑ, Ð¿Ð°ÑÑÐ¸ Ð¸ Ð¿ÑÐ¾ÑÐ¸Ð»Ð¸ Ð°Ð²ÑÐ¾Ð·Ð°Ð³ÑÑÐ·ÐºÐ¸ Ð½Ðµ Ð·Ð°ÑÑÐ°Ð³Ð¸Ð²Ð°ÑÑÑÑ.","ÐÑÑ Ð²ÑÐ³Ð»ÑÐ´Ð¸Ñ ÑÐ¾ÑÐ¾ÑÐ¾.","Ð§ÑÐ¾-ÑÐ¾ ÑÑÐµÐ±ÑÐµÑ Ð²Ð½Ð¸Ð¼Ð°Ð½Ð¸Ñ - ÑÐ¼. Ð¿Ð¾Ð´ÑÐ¾Ð±Ð½Ð¾ÑÑÐ¸ Ð½Ð¸Ð¶Ðµ.","Ð¤Ð°Ð¹Ð» .mc4, .shn Ð¸Ð»Ð¸ .json Ñ Ð²Ð°ÑÐµÐ³Ð¾ ÐºÐ¾Ð¼Ð¿ÑÑÑÐµÑÐ°.","ÐÐ¾Ð±Ð°Ð²ÑÑÐµ ÑÐ°Ð¹Ð» Ð²Ð¾ Ð²ÐºÐ»Ð°Ð´ÐºÐµ Â«Ð¤Ð°Ð¹Ð»ÑÂ» Ð¸Ð»Ð¸ Â«ÐÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸Â»","ÐÑÐ¾ÑÐ¼Ð»ÐµÐ½Ð¸Ðµ","ÐÑÐ¾ÑÐ¼Ð»ÐµÐ½Ð¸Ðµ, Ð´Ð²Ð¸Ð¶Ð¾Ðº ÑÐ¸ÑÐ¾Ð² Ð¸ Ð³ÐµÐ¹Ð¼Ð¿Ð°Ð´. ÐÐ·Ð¼ÐµÐ½ÐµÐ½Ð¸Ñ ÑÐ¾ÑÑÐ°Ð½ÑÑÑÑÑ ÑÑÐ°Ð·Ñ.","Ð¤Ð°Ð¹Ð» ÑÐ¸ÑÐ¾Ð²","Ð¤Ð°Ð¹Ð»Ñ ÑÐ¸ÑÐ¾Ð² Ð´Ð»Ñ ÑÑÐ¾Ð¹ Ð¸Ð³ÑÑ","CheatRunner Ð¿Ð¾ÐºÐ°Ð·ÑÐ²Ð°ÐµÑ Ð¸Ð³ÑÑ, ÑÑÑÐ°Ð½Ð¾Ð²Ð»ÐµÐ½Ð½ÑÐµ Ð½Ð° ÑÑÐ¾Ð¹ ÐºÐ¾Ð½ÑÐ¾Ð»Ð¸. Ð£ÑÑÐ°Ð½Ð¾Ð²Ð¸ÑÐµ Ð¸Ð³ÑÑ Ð¸ Ð¾Ð±Ð½Ð¾Ð²Ð¸ÑÐµ ÑÐ¿Ð¸ÑÐ¾Ðº.","Ð§Ð¸ÑÑ Ð¸ Ð¿Ð°ÑÑÐ¸ Ð½ÐµÐ»ÑÐ·Ñ Ð¿ÑÐ¸Ð¼ÐµÐ½ÑÑÑ, Ð¿Ð¾ÐºÐ° Ð¾Ð½ Ð½Ðµ Ð²ÐºÐ»ÑÑÑÐ½.","ÐÑÐ¾Ð²ÐµÑÐºÐ°...","ÐÑÐ±ÑÐ°ÑÑ Ð´ÑÑÐ³Ð¾Ð¹ ÑÐ°Ð¹Ð»","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð³ÑÑÐ·Ð¸ÑÑ","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð³ÑÑÐ·Ð¸ÑÑ ÑÐ°Ð¹Ð» ÑÐ¸ÑÐ¾Ð²","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð³ÑÑÐ·Ð¸ÑÑ Ð¿Ð°ÑÑÐ¸","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð³ÑÑÐ·Ð¸ÑÑ Ð½Ð°ÑÑÑÐ¾Ð¹ÐºÐ¸","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð³ÑÑÐ·Ð¸ÑÑ Ð²Ð°ÑÐ¸ Ð¸Ð³ÑÑ","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð¿Ð¾Ð»ÑÑÐ¸ÑÑ ÑÐµÐºÑÑÐµÐµ ÑÐ¾ÑÑÐ¾ÑÐ½Ð¸Ðµ ÑÐ¸ÑÐ¾Ð².","Ð¡ÐºÐ°ÑÐ°Ð¹ÑÐµ ÑÐ¸ÑÑ ÑÐ¾Ð¾Ð±ÑÐµÑÑÐ²Ð° Ð²Ð¾ Ð²ÐºÐ»Ð°Ð´ÐºÐµ Â«ÐÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸Â» Ð¸Ð»Ð¸ Ð´Ð¾Ð±Ð°Ð²ÑÑÐµ ÑÐ°Ð¹Ð» Ñ ÐºÐ¾Ð¼Ð¿ÑÑÑÐµÑÐ°.","Ð¡ÐºÐ°ÑÐ°Ð¹ÑÐµ ÑÐ°Ð¹Ð» Ð²Ð¾ Ð²ÐºÐ»Ð°Ð´ÐºÐµ Â«ÐÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸Â» Ð¸Ð»Ð¸ Ð¿ÐµÑÐµÑÐ°ÑÐ¸ÑÐµ ÐµÐ³Ð¾ Ð½Ð¸Ð¶Ðµ.","Ð¡ÐºÐ°ÑÐ¸Ð²Ð°Ð¹ÑÐµ Ð³Ð¾ÑÐ¾Ð²ÑÐµ ÑÐ¸ÑÑ Ð¸ Ð¿Ð°ÑÑÐ¸ Ð´Ð»Ñ Ð²Ð°ÑÐ¸Ñ Ð¸Ð³Ñ. ÐÐ°Ð³ÑÑÐ·ÐºÐ¸ Ð¿ÑÐ¾Ð´Ð¾Ð»Ð¶Ð°ÑÑÑÑ, Ð¿Ð¾ÐºÐ° Ð²Ñ Ð¿Ð¾Ð»ÑÐ·ÑÐµÑÐµÑÑ Ð¿Ð°Ð½ÐµÐ»ÑÑ.","Ð¡ÐºÐ°ÑÐ°Ð¹ÑÐµ Ð¾ÑÐ¸ÑÐ¸Ð°Ð»ÑÐ½ÑÐ¹ Ð½Ð°Ð±Ð¾Ñ Ð¿Ð°ÑÑÐµÐ¹ Ð²Ð¾ Ð²ÐºÐ»Ð°Ð´ÐºÐµ Â«ÐÑÑÐ¾ÑÐ½Ð¸ÐºÐ¸Â» Ð¸Ð»Ð¸ Ð¿Ð¾Ð»Ð¾Ð¶Ð¸ÑÐµ ÑÐ²Ð¾Ð¸ XML-Ð¿Ð°ÑÑÐ¸ Ð² /data/cheatrunner/patches/xml.","ÐÐ°Ð³ÑÑÐ·ÐºÐ°","ÐÐ°Ð³ÑÑÐ·ÐºÐ° {p}%","ÐÐ°Ð³ÑÑÐ·ÐºÐ¸","ÐÐ²Ð¸Ð¶Ð¾Ðº","ÐÐ¾ÐºÐ°Ð·Ð°Ð½Ñ Ð¿Ð¾ÑÐ»ÐµÐ´Ð½Ð¸Ðµ ÑÑÑÐ¾ÐºÐ¸","ÐÐ¾Ð»ÑÑÐ¸ÑÑ ÑÐ¸ÑÑ","Ð¡Ð¾ÑÑÐ¾ÑÐ½Ð¸Ðµ","ÐÐ¸Ð±Ð»Ð¸Ð¾ÑÐµÐºÐ°","Ð¢ÐµÐºÑÑÐµÐµ ÑÐ¾ÑÑÐ¾ÑÐ½Ð¸Ðµ Ð´Ð²Ð¸Ð¶ÐºÐ° ÑÐ¸ÑÐ¾Ð² Ð¸ Ð·Ð°Ð¿Ð¸ÑÐ¸ Ð² Ð¿Ð°Ð¼ÑÑÑ, Ð° ÑÐ°ÐºÐ¶Ðµ ÑÑÐ¾ Ð´ÐµÐ»Ð°Ð» CheatRunner.","ÐÐ¾Ð²Ð¾ÑÑÐ¸, Ð¿Ð¾Ð¼Ð¾ÑÑ Ð¸ Ð»ÑÐ´Ð¸, ÑÑÐ¾ÑÑÐ¸Ðµ Ð·Ð° CheatRunner.","ÐÐ»Ñ ÑÑÐ¾Ð¹ Ð¸Ð³ÑÑ ÐµÑÑ Ð½ÐµÑ ÑÐ°Ð¹Ð»Ð° ÑÐ¸ÑÐ¾Ð²","ÐÐµÑ ÑÐ¸ÑÐ¾Ð² Ð¿Ð¾ Ð·Ð°Ð¿ÑÐ¾ÑÑ Â«{q}Â».","Ð¤Ð°Ð¹Ð»Ð° Ð¿Ð¾ÐºÐ° Ð½ÐµÑ","ÐÐ³ÑÑ Ð¿Ð¾ÐºÐ° Ð½Ðµ Ð½Ð°Ð¹Ð´ÐµÐ½Ñ","ÐÐ¾ÐºÐ°Ð»ÑÐ½ÑÐ¹ ÑÐ°Ð¹Ð» Ð½Ðµ Ð½Ð°Ð¹Ð´ÐµÐ½","ÐÐ»Ñ ÑÑÐ¾Ð¹ Ð¸Ð³ÑÑ Ð½ÐµÑ Ð¿Ð°ÑÑÐµÐ¹","Ð¡ÐµÐ¹ÑÐ°Ñ Ð·Ð°Ð¿ÑÑÐµÐ½Ð°","ÐÑÐºÑÑÑÑ Ð½Ð°ÑÑÑÐ¾Ð¹ÐºÐ¸","ÐÑÐ¸Ð¾ÑÑÐ°Ð½Ð¾Ð²Ð»ÐµÐ½Ð¾, Ð¿Ð¾ÐºÐ° Ð²Ñ ÑÐ¸ÑÐ°ÐµÑÐµ","ÐÑÐµÐ´Ð¿Ð¾ÑÑÐµÐ½Ð¸Ñ","Â«Ð¡Ð½Ð¸Ð¶ÐµÐ½Ð½ÑÐµÂ» ÑÐ±Ð¸ÑÐ°ÑÑ Ð²ÑÐµ Ð°Ð½Ð¸Ð¼Ð°ÑÐ¸Ð¸. Ð­ÑÐ¾ Ð·Ð½Ð°ÑÐµÐ½Ð¸Ðµ Ð¿Ð¾ ÑÐ¼Ð¾Ð»ÑÐ°Ð½Ð¸Ñ Ð² Ð±ÑÐ°ÑÐ·ÐµÑÐµ PS5.","Ð¤Ð°Ð¹Ð»Ñ ÑÑÐ°Ð½ÑÑÑÑ Ð² /data/cheatrunner/cheats. Ð¢ÑÐµÐ¹Ð½ÐµÑ ÑÐ¸ÑÐ°ÐµÑ ÑÐ°Ð¹Ð» Ñ Ð¿Ð¾Ð¼ÐµÑÐºÐ¾Ð¹ Â«ÐÑÐ¿Ð¾Ð»ÑÐ·ÑÐµÑÑÑÂ».","ÐÑÐ¿ÑÐ°Ð²ÑÑÐµ Ð¾ÑÑÑÑ, ÐµÑÐ»Ð¸ ÑÑÐ¾-ÑÐ¾ Ð½Ðµ ÑÐ°Ð±Ð¾ÑÐ°ÐµÑ.","ÐÐ°Ð¿ÑÑÑÐ¸ÑÐµ, ÑÑÐ¾Ð±Ñ Ð¿Ð¾Ð»ÑÐ·Ð¾Ð²Ð°ÑÑÑÑ ÑÐ¸ÑÐ°Ð¼Ð¸","ÐÐ°Ð¿ÑÑÑÐ¸ÑÐµ Ð¸Ð³ÑÑ, ÑÑÐ¾Ð±Ñ Ð¿Ð¾Ð»ÑÐ·Ð¾Ð²Ð°ÑÑÑÑ ÐµÑ ÑÐ¸ÑÐ°Ð¼Ð¸.","Ð¢ÐµÑÐ½Ð¸ÑÐµÑÐºÐ¸Ðµ Ð¿Ð¾Ð´ÑÐ¾Ð±Ð½Ð¾ÑÑÐ¸","ÐÐ²Ð¸Ð¶Ð¾Ðº ÑÐ¸ÑÐ¾Ð² Ð²ÑÐºÐ»ÑÑÐµÐ½.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð¿ÑÐ¾ÑÐ¸ÑÐ°ÑÑ ÑÐµÐºÑÑÐ¸Ðµ Ð·Ð½Ð°ÑÐµÐ½Ð¸Ñ, Ð¿Ð¾ÑÑÐ¾Ð¼Ñ Ð½Ð¸ÑÐµÐ³Ð¾ Ð½Ðµ Ð¿Ð¾ÐºÐ°Ð·Ð°Ð½Ð¾, ÑÑÐ¾Ð±Ñ Ð½Ðµ Ð²Ð²Ð¾Ð´Ð¸ÑÑ Ð² Ð·Ð°Ð±Ð»ÑÐ¶Ð´ÐµÐ½Ð¸Ðµ.","ÐÑÐ½Ð¾Ð²Ð½Ð¾Ðµ Ð´Ð»Ñ Ð¿ÑÐ¸Ð¼ÐµÐ½ÐµÐ½Ð¸Ñ ÑÐ¸ÑÐ¾Ð² Ð¸ Ð¿Ð°ÑÑÐµÐ¹.","Ð¤Ð°Ð¹Ð» Ð½Ð° Ð¼ÐµÑÑÐµ, Ð½Ð¾ ÐµÐ³Ð¾ Ð½Ðµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð¿ÑÐ¾ÑÐ¸ÑÐ°ÑÑ. ÐÐ½ Ð¼Ð¾Ð¶ÐµÑ Ð±ÑÑÑ Ð¿Ð¾Ð²ÑÐµÐ¶Ð´ÑÐ½ Ð¸Ð»Ð¸ ÐµÑÑ ÐºÐ¾Ð¿Ð¸ÑÑÐµÑÑÑ.","Ð¤Ð°Ð¹Ð» Ð·Ð°Ð³ÑÑÐ¶ÐµÐ½, Ð½Ð¾ Ð¾Ð½ Ð¿ÑÑÑ. ÐÐ¾Ð¿ÑÐ¾Ð±ÑÐ¹ÑÐµ Ð´ÑÑÐ³Ð¾Ð¹ ÑÐ°Ð¹Ð» Ð´Ð»Ñ ÑÑÐ¾Ð¹ Ð¸Ð³ÑÑ.","Ð¡Ð¿Ð¸ÑÐ¾Ðº Ð¸Ð³Ñ ÑÐµÐ¹ÑÐ°Ñ Ð½ÐµÐ´Ð¾ÑÑÑÐ¿ÐµÐ½. Ð£Ð±ÐµÐ´Ð¸ÑÐµÑÑ, ÑÑÐ¾ ÐºÐ¾Ð½ÑÐ¾Ð»Ñ Ð²ÐºÐ»ÑÑÐµÐ½Ð°, Ð¸ Ð¿Ð¾Ð²ÑÐ¾ÑÐ¸ÑÐµ.","ÐÐµ ÑÐ´Ð°Ð»Ð¾ÑÑ Ð¿ÑÐ¾ÑÐ¸ÑÐ°ÑÑ ÑÐ°Ð¹Ð»Ñ Ð¿Ð°ÑÑÐµÐ¹ Ð´Ð»Ñ ÑÑÐ¾Ð¹ Ð¸Ð³ÑÑ. ÐÐ¾Ð²ÑÐ¾ÑÐ¸ÑÐµ ÑÑÑÑ Ð¿Ð¾Ð·Ð¶Ðµ.","ÐÐµÑÐµÐºÐ»ÑÑÐ°ÑÐµÐ»Ð¸ Ð¼Ð¾Ð³ÑÑ Ð±ÑÑÑ Ð½ÐµÐ°ÐºÑÑÐ°Ð»ÑÐ½Ñ.","Ð¢ÐµÐ¼Ð°, ÑÑÑÐµÐºÑÑ Ð¸ ÑÐ·ÑÐº ÑÑÐ¾Ð¹ Ð¿Ð°Ð½ÐµÐ»Ð¸.","Ð ÑÑÐ¾Ð¼ ÑÐ°Ð¹Ð»Ðµ Ð½ÐµÑ ÑÐ¸ÑÐ¾Ð²","ÐÐ¾Ð²ÑÐ¾ÑÐ¸ÑÐµ Ð½Ð¸Ð¶Ðµ","ÐÐºÐ»ÑÑÐ¸ÑÐµ Ð² Ð½Ð°ÑÑÑÐ¾Ð¹ÐºÐ°Ñ","ÐÐ¿Ð°ÑÐ½Ð°Ñ Ð·Ð¾Ð½Ð°","Ð Ð¿Ð¾ÑÐ»ÐµÐ´Ð½Ð¸Ð¼","Ð£Ð´Ð°Ð»Ð¸ÑÑ Ð¸ÑÐ¿Ð¾Ð»ÑÐ·ÑÐµÐ¼ÑÐ¹ ÑÐ°Ð¹Ð»","ÐÑÑÐ¾ÑÐ½Ð¸Ðº ÑÐ¸ÑÐ¾Ð²","Ð¤Ð¸Ð»ÑÑÑ Ð¸Ð³Ñ","ÐÑÐºÑÑÑÑ Ð¼Ð¾Ð¸ Ð¸Ð³ÑÑ","ÐÐ²ÑÐ¾ÑÑ","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"th-TH":["à¸à¸µà¸à¸«à¸à¸¶à¹à¸à¸à¸³à¹à¸«à¹à¹à¸à¸¡à¹à¸à¸£à¸à¹à¸à¸à¸£à¸±à¹à¸à¸à¹à¸­à¸","à¹à¸à¸µà¹à¸¢à¸§à¸à¸±à¸","à¸à¸²à¸£à¸à¸³à¸à¸²à¸","à¸à¸²à¸£à¸à¸³à¸à¸²à¸","à¹à¸à¸´à¹à¸¡à¹à¸à¸¥à¹","à¹à¸à¸´à¹à¸¡à¸à¸µà¸","à¹à¸à¸´à¹à¸¡à¹à¸à¸£à¸²à¸¢à¸à¸²à¸£à¹à¸à¸£à¸","à¹à¸à¸´à¹à¸¡à¸£à¸²à¸¢à¸à¸²à¸£ â CheatRunner à¹à¸à¹à¸¡à¸à¸¹à¸à¸±à¸§à¹à¸¥à¸·à¸­à¸à¸à¸­à¸à¹à¸à¹à¸¥à¸°à¹à¸à¸¡ à¹à¸¥à¸°à¸à¸¸à¸à¸à¸¸à¹à¸¡à¸à¸µà¹à¹à¸à¸´à¸à¹à¸à¸£à¸à¹à¸à¸­à¸£à¹à¸à¸­à¸à¹à¸à¸¡à¸à¸µà¹à¸à¸³à¸¥à¸±à¸à¹à¸¥à¹à¸","à¸à¸±à¹à¸à¸ªà¸¹à¸","à¸à¸±à¹à¸à¸«à¸¡à¸","à¹à¸à¸´à¸à¸ªà¸à¸±à¸à¸ªà¸à¸¸à¸à¸à¸±à¹à¸à¸«à¸¡à¸à¸à¸°à¸à¸³à¹à¸à¹à¸à¹à¹à¸à¸·à¹à¸­à¸à¸·à¹à¸­ PS5 à¹à¸à¸´à¸£à¹à¸¡à¹à¸§à¸£à¹à¸à¹à¸³ (1.xxâ4.xx) à¸ªà¸³à¸«à¸£à¸±à¸à¸à¸²à¸£à¸§à¸´à¸à¸±à¸¢ à¸à¸±à¸à¸à¸² à¹à¸¥à¸°à¸­à¸±à¸à¹à¸à¸£à¸à¹à¸à¸­à¸à¸²à¸à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸±à¹à¸à¸«à¸¡à¸à¸¥à¹à¸¡à¹à¸«à¸¥à¸§","à¹à¸à¸¡à¸à¸±à¹à¸à¸«à¸¡à¸","à¸­à¸±à¸à¹à¸à¸à¹à¸à¸¥à¹à¸à¸µà¹à¹à¸à¸¥à¸µà¹à¸¢à¸à¹à¸à¸¥à¸à¸à¹à¸§à¸¢","à¹à¸à¹à¹à¸¥à¹à¸§","à¹à¸à¹","à¹à¸­à¸","à¹à¸¥à¸·à¸­à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´","à¸à¸¥à¸±à¸","à¹à¸£à¸µà¸¢à¸à¸à¸¹à¸à¸¥à¸±à¸à¹à¸à¸¡à¸à¸­à¸à¸à¸¸à¸","à¸à¸¸à¹à¸¡","Buy Me a Coffee","à¸à¸µà¸","à¹à¸­à¸à¸à¸´à¸à¸à¸µà¸","CheatRunner - à¹à¸à¸£à¸à¹à¸à¸­à¸£à¹à¸à¸µà¸à¸ªà¸³à¸«à¸£à¸±à¸ PS5 à¸à¸­à¸à¸à¸¸à¸!","à¸à¸µà¸","à¸à¸µà¸à¸à¸°à¸à¸¹à¸à¸à¸±à¸à¹à¸§à¹à¸à¸²à¸¡à¹à¸§à¸¥à¸²à¸à¸µà¹à¸«à¸¥à¸±à¸à¹à¸£à¸´à¹à¸¡à¹à¸à¸¡","à¹à¸à¸¡à¸à¸µà¹à¸¡à¸µà¸à¸µà¸à¸à¹à¸­à¸","à¹à¸¥à¸·à¸­à¸à¹à¸à¸£à¸·à¸­à¸à¹à¸²à¸¢","Circle","à¸¥à¹à¸²à¸à¸à¸±à¸§à¸à¸£à¸­à¸","à¸¥à¹à¸²à¸à¸à¸²à¸£à¹à¸à¹à¸à¹à¸à¸·à¸­à¸","à¸¥à¹à¸²à¸à¸à¸²à¸£à¸à¹à¸à¸«à¸²","à¸à¸´à¸","à¸¢à¹à¸­","à¸¢à¹à¸­à¹à¸¡à¸à¸¹","à¸à¸¸à¸à¸ªà¸µà¸à¸­à¸à¹à¸à¸à¸à¸­à¸£à¹à¸à¸à¸µà¹","à¸à¸¸à¸¡à¸à¸","à¸à¸¥à¸±à¸à¸à¸µà¸à¸à¸²à¸à¸à¸¸à¸¡à¸à¸","à¸à¸¸à¹à¸¡à¸¥à¸±à¸à¸à¸­à¸à¹à¸à¸£à¸¥à¹à¸¥à¸­à¸£à¹à¹à¸¥à¸°à¹à¸¡à¸à¸¹à¹à¸à¸¡","à¸à¸±à¸à¸¥à¸­à¸","à¸à¸±à¸à¸¥à¸­à¸ URL","à¸à¸±à¸à¸¥à¸­à¸à¸à¸±à¹à¸à¸«à¸¡à¸","à¸à¸±à¸à¸¥à¸­à¸à¹à¸à¸¥à¹à¸à¸­à¸£à¹à¸à¸µà¸","à¸à¸±à¸à¸¥à¸­à¸à¸¥à¸´à¸à¸à¹","à¸à¸£à¸§à¸à¸à¸±à¸à¸à¸²à¸£à¹à¸à¸£à¸","à¸à¸³à¹à¸«à¹à¹à¸à¸¡à¹à¸à¸£à¸à¸à¸£à¸±à¹à¸à¸à¹à¸­à¸","Cross","à¸à¸£à¸´à¸à¹à¸","à¸ªà¸à¸±à¸à¸ªà¸à¸¸à¸à¸à¹à¸§à¸¢à¸à¸£à¸´à¸à¹à¸","à¸¡à¸·à¸","à¸¥à¸","à¸à¸´à¸à¸à¸±à¹à¸à¸«à¸¡à¸","à¸¥à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸¸à¸à¹à¸«à¸¥à¹à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸µà¸à¸à¸²à¸à¸à¸¸à¸¡à¸à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸¥à¹à¸¡à¹à¸«à¸¥à¸§","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¹à¸à¸¥à¹à¸à¸µà¸à¸ªà¸³à¹à¸£à¹à¸à¸£à¸¹à¸à¸ªà¸³à¸«à¸£à¸±à¸à¹à¸à¸¡à¸à¸­à¸à¸à¸¸à¸ à¸à¸±à¸à¸à¸¶à¸à¹à¸§à¹à¸à¸µà¹ /data/cheatrunner/cheats à¹à¸¥à¸°à¸à¸²à¸£à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸°à¸à¸³à¸à¸²à¸à¸à¹à¸­à¹à¸à¸à¸·à¹à¸à¸«à¸¥à¸±à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸µà¹à¹à¸¥à¸·à¸­à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¹à¸¥à¹à¸§","à¸§à¸²à¸à¹à¸à¸¥à¹ .mc4, .shn à¸«à¸£à¸·à¸­ .json à¸à¸µà¹à¸à¸µà¹ à¸«à¸£à¸·à¸­à¸à¸¥à¸´à¸à¹à¸à¸·à¹à¸­à¹à¸¥à¸·à¸­à¸","à¹à¸à¸´à¸à¹à¸à¹à¸à¸¸à¹à¸¡à¸¥à¸±à¸à¸à¸­à¸à¹à¸à¸£à¸¥à¹à¸¥à¸­à¸£à¹","à¹à¸à¸´à¸à¹à¸à¹à¸à¸¸à¹à¸¡à¸¥à¸±à¸","à¹à¸­à¸à¸à¸´à¸à¹à¸à¸´à¸","Enter à¹à¸à¸·à¹à¸­à¹à¸à¸´à¸","à¹à¸à¸à¸²à¸°à¸à¹à¸­à¸à¸´à¸à¸à¸¥à¸²à¸","à¸à¸£à¸à¸à¸¸à¸à¸à¸£à¸°à¸à¸²à¸£","à¸¥à¹à¸¡à¹à¸«à¸¥à¸§","à¸£à¸²à¸¢à¸à¸²à¸£à¹à¸à¸£à¸","à¹à¸à¸¥à¹","à¸à¸£à¸­à¸","à¸à¸£à¸­à¸à¸à¸±à¸à¸à¸¶à¸","à¸à¸³à¹à¸à¸£à¸·à¹à¸­à¸à¸«à¸¡à¸²à¸¢à¸à¸µà¸à¸«à¸²à¸à¹à¸à¸¡à¸à¸´à¸à¸à¸±à¸à¸à¸µà¸«à¸¥à¸±à¸à¹à¸à¸´à¸à¹à¸à¹","à¹à¸à¸·à¹à¸­à¸à¸¹à¹à¹à¸à¹ à¹à¸à¸¢à¸à¸¹à¹à¹à¸à¹","à¸à¸±à¸à¸à¸±à¸à¹à¸à¹","à¹à¸à¸¡","à¹à¸à¸¡à¸à¸µà¹à¸¡à¸µà¸à¸µà¸","à¹à¸à¸¡à¸à¸µà¹à¸¡à¸µà¹à¸à¸à¸à¹","à¸à¹à¸§à¸à¹à¸§à¹à¸à¸£à¸°à¸«à¸§à¹à¸²à¸à¸à¸µà¸ (à¸¡à¸´à¸¥à¸¥à¸´à¸§à¸´à¸à¸²à¸à¸µ)","à¸à¸±à¹à¸§à¹à¸","à¸à¸à¸à¹à¸²à¸à¸à¸£à¹à¸­à¸¡à¸à¸±à¸à¹à¸à¸·à¹à¸­à¹à¸£à¸µà¸¢à¸à¸à¸¸à¹à¸¡à¸¥à¸±à¸","à¹à¸§à¸¥à¸²à¸à¸à¸à¹à¸²à¸ (à¸¡à¸´à¸¥à¸¥à¸´à¸§à¸´à¸à¸²à¸à¸µ)","à¸à¸à¸ªà¸­à¸à¸à¸¸à¹à¸¡à¸à¹à¸²à¸à¸à¸£à¹à¸­à¸¡à¸à¸±à¸à¹à¸à¸·à¹à¸­à¹à¸à¸´à¸à¹à¸à¸£à¸à¹à¸à¸­à¸£à¹à¸à¸­à¸à¹à¸à¸¡à¸à¸µà¹à¸à¸³à¸¥à¸±à¸à¹à¸¥à¹à¸","à¹à¸à¸¥à¹à¸à¸à¸«à¸à¹à¸²à¸à¸­à¸«à¸¥à¸±à¸","à¹à¸à¸·à¹à¸­à¸¡à¹à¸à¹à¸²à¸à¸±à¸ UI à¸à¸­à¸à¸£à¸°à¸à¸ à¸à¸´à¸à¹à¸§à¹à¹à¸à¸¢à¸à¹à¸²à¹à¸£à¸´à¹à¸¡à¸à¹à¸ - à¸à¸à¸à¸²à¸à¹à¸à¸´à¸£à¹à¸¡à¹à¸§à¸£à¹à¸­à¸²à¸à¸à¸³à¹à¸«à¹à¸«à¸à¹à¸²à¸à¸­à¸«à¸¥à¸±à¸à¹à¸à¸£à¸","à¸à¹à¸­à¸à¸à¸à¸à¸¸à¸à¸à¸¸à¹à¸¡à¸à¹à¸²à¸à¸à¸²à¸à¹à¸à¹à¸²à¹à¸à¸à¸¶à¸à¸à¸°à¸à¸³à¸à¸²à¸","à¸£à¸°à¸à¸±à¸à¸£à¸²à¸¢à¸¥à¸°à¹à¸­à¸µà¸¢à¸à¸à¸µà¹à¸à¸±à¸à¸à¸¶à¸à¸¥à¸à¸¥à¹à¸­à¸","à¸«à¸²à¸ CheatRunner à¸¡à¸µà¸à¸£à¸°à¹à¸¢à¸à¸à¹à¸à¸±à¸à¸à¸¸à¸ à¹à¸à¸£à¸à¸à¸´à¸à¸²à¸£à¸à¸²à¸ªà¸à¸±à¸à¸ªà¸à¸¸à¸à¸à¸²à¸£à¸à¸±à¸à¸à¸²à¹à¸à¸£à¹à¸à¸à¸à¹à¸à¸µà¹! ð«¶","à¸à¸³à¸¥à¸±à¸à¹à¸à¹à¸à¸²à¸","à¸à¸´à¸à¸à¸±à¹à¸à¹à¸à¸¥à¹ CheatRunner à¸à¸à¸«à¸à¹à¸²à¸à¸­à¸«à¸¥à¸±à¸à¸à¸­à¸ PS5 à¹à¸¡à¸·à¹à¸­à¸à¸¹à¸","à¹à¸à¹à¸²à¸£à¹à¸§à¸¡ Discord","Ko-Fi à¸à¸²à¹à¸ à¸à¸£à¸´à¸à¹à¸ PIX","à¸ à¸²à¸©à¸²","à¸ à¸²à¸©à¸²à¸à¸­à¸à¹à¸à¸à¸à¸­à¸£à¹à¸à¸à¸µà¹","à¹à¸¥à¹à¸à¸¥à¹à¸²à¸ªà¸¸à¸","à¸à¹à¸²à¸¢","à¸ªà¸§à¹à¸²à¸","à¸ªà¸à¸²à¸à¸°à¸à¸­à¸à¹à¸­à¸à¸à¸´à¸à¸à¸µà¸à¹à¸¥à¸°à¸à¸²à¸£à¹à¸à¸à¸à¹à¸«à¸à¹à¸§à¸¢à¸à¸§à¸²à¸¡à¸à¸³","à¹à¸à¸¥à¹à¸à¸µà¸à¹à¸à¹à¸à¸£à¸·à¹à¸­à¸","à¸£à¸°à¸à¸±à¸à¸¥à¹à¸­à¸","à¸à¸±à¸§à¸à¸£à¸­à¸à¸£à¸°à¸à¸±à¸à¸¥à¹à¸­à¸","à¸à¸±à¸à¸à¸¶à¸","à¸«à¸¥à¸±à¸","à¸ªà¸§à¸´à¸à¸à¹à¸«à¸¥à¸±à¸à¸ªà¸³à¸«à¸£à¸±à¸à¹à¸à¹à¸à¸µà¸à¹à¸¥à¸°à¹à¸à¸à¸à¹","à¸à¸²à¸£à¹à¸à¹à¹à¸à¸¡à¸²à¸ªà¹à¸à¸­à¸£à¹à¹à¸à¹à¸","à¸­à¸²à¸à¹à¸¡à¹à¸à¸£à¸à¸à¸±à¸à¹à¸§à¸­à¸£à¹à¸à¸±à¸à¹à¸à¸¡à¸à¸µà¹","à¸à¸²à¸£à¹à¸à¸à¸à¹à¸«à¸à¹à¸§à¸¢à¸à¸§à¸²à¸¡à¸à¸³","à¹à¸à¸µà¹à¸¢à¸à¸à¸·à¸","à¹à¸§à¸¥à¸²à¸à¸±à¹à¸à¸à¹à¸³à¸£à¸°à¸«à¸§à¹à¸²à¸à¸à¸²à¸£à¸ªà¸¥à¸±à¸à¸à¸µà¸à¸ªà¸­à¸à¸à¸£à¸±à¹à¸","à¸à¸·à¹à¸­ A-Z","à¸à¸·à¹à¸­ Z-A","à¸¢à¸±à¸à¹à¸¡à¹à¸¡à¸µà¹à¸à¸¥à¹à¸à¸µà¸à¸ªà¸³à¸«à¸£à¸±à¸à¹à¸à¸¡à¸à¸µà¹","à¸¢à¸±à¸à¹à¸¡à¹à¸¡à¸µà¸£à¸²à¸¢à¸à¸²à¸£à¹à¸à¸£à¸ à¹à¸à¸°à¸à¸²à¸§à¸à¸à¹à¸à¸¡à¹à¸à¸·à¹à¸­à¹à¸à¸´à¹à¸¡","à¹à¸¡à¹à¹à¸à¹à¸à¸³à¸à¸²à¸","à¹à¸à¸´à¸","à¹à¸à¸´à¸à¸à¸à¹à¸à¸£à¸¨à¸±à¸à¸à¹","à¹à¸à¸´à¸à¸à¸à¹à¸à¸£à¸¨à¸±à¸à¸à¹ (QR à¹à¸à¹à¸)","à¹à¸à¸´à¸à¹à¸à¸à¸à¸­à¸£à¹à¸à¸à¸à¹à¸à¸£à¸¨à¸±à¸à¸à¹à¸à¸­à¸à¸à¸¸à¸","à¹à¸à¸´à¸à¹à¸à¸£à¸à¹à¸à¸­à¸£à¹","à¸à¸±à¸§à¹à¸¥à¸·à¸­à¸","à¹à¸§à¸­à¸£à¹à¸à¸±à¸à¸­à¸·à¹à¸","à¸«à¸à¹à¸²","à¹à¸à¸à¸à¹","à¹à¸¥à¹à¸","QR à¹à¸à¹à¸","QR à¹à¸à¹à¸à¸ªà¸³à¸«à¸£à¸±à¸à¸à¸µà¹à¸­à¸¢à¸¹à¹à¹à¸à¸à¸à¸­à¸£à¹à¸","à¹à¸à¹à¸à¸µà¸à¸à¸µà¹à¹à¸à¸´à¸à¸­à¸¢à¸¹à¹à¸à¹à¸³à¸à¸¸à¸à¸à¸£à¸±à¹à¸à¸à¸µà¹à¹à¸à¸¡à¸à¸µà¹à¹à¸£à¸´à¹à¸¡à¸à¸³à¸à¸²à¸","à¸à¸£à¹à¸­à¸¡","à¸¥à¹à¸²à¸ªà¸¸à¸","à¸£à¸µà¹à¸à¸£à¸","à¸£à¸µà¹à¸à¸£à¸à¸£à¸²à¸¢à¸à¸·à¹à¸­à¹à¸à¸¡","à¹à¸£à¸´à¹à¸¡à¹à¸«à¸¡à¹","à¸£à¸²à¸¢à¸à¸²à¸à¸à¸±à¸à¸«à¸²","à¸ªà¹à¸à¸à¹à¸à¸¡à¸à¸µà¹à¸à¸´à¸à¸à¸±à¹à¸à¹à¸«à¸¡à¹","à¸à¸·à¸à¸à¹à¸²à¹à¸£à¸´à¹à¸¡à¸à¹à¸","à¹à¸à¹à¸à¸µà¸à¸à¸µà¹à¸à¹à¸­à¸à¸à¸¶à¹à¸à¸¡à¸²à¸ªà¹à¸à¸­à¸£à¹à¹à¸à¹à¸à¹à¸à¸¢à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´","à¸à¸·à¸à¸à¹à¸²","à¸à¸§à¸²","à¸à¸³à¸¥à¸±à¸à¸à¸³à¸à¸²à¸","à¸à¸³à¸¥à¸±à¸à¸à¸³à¸à¸²à¸à¸­à¸¢à¸¹à¹","à¸à¸±à¸à¸à¸¶à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´","à¸ªà¹à¸à¸à¸à¹à¸§à¸¢à¸à¸¥à¹à¸­à¸à¹à¸à¸£à¸¨à¸±à¸à¸à¹ à¹à¸à¸£à¸¨à¸±à¸à¸à¹à¸à¹à¸­à¸à¸­à¸¢à¸¹à¹à¹à¸ Wi-Fi à¹à¸à¸µà¸¢à¸§à¸à¸±à¸ PS5","à¸à¹à¸à¸«à¸²","à¸à¹à¸à¸«à¸²à¸à¹à¸§à¸¢à¸à¸·à¹à¸­à¸«à¸£à¸·à¸­ Title ID","à¸à¹à¸à¸«à¸²à¹à¸à¸¡","à¸à¹à¸à¸«à¸²à¹à¸à¸¡ à¸«à¸à¹à¸² à¹à¸¥à¸°à¸à¸²à¸£à¸à¸³à¸à¸²à¸","à¹à¸¥à¸·à¸­à¸à¸à¸µà¹à¸­à¸¢à¸¹à¹à¹à¸à¸·à¹à¸­à¸à¸±à¸à¸¥à¸­à¸à¸à¸±à¹à¸à¸«à¸¡à¸","à¸à¸²à¸£à¸à¸±à¹à¸à¸à¹à¸²","à¹à¸ªà¸à¸ QR à¹à¸à¹à¸","à¸à¸´à¸à¹à¸à¸¢à¹à¹à¸«à¸¥à¸","à¹à¸£à¸µà¸¢à¸à¸¥à¸³à¸à¸±à¸à¹à¸à¸¡","à¸à¸­à¸£à¹à¸ª","à¹à¸«à¸¥à¹à¸à¸à¸µà¹à¸¡à¸²","à¸à¸§à¸²à¸¡à¹à¸£à¹à¸§","Square","à¹à¸£à¸´à¹à¸¡à¹à¸à¸¡","à¹à¸£à¸´à¹à¸¡à¹à¸à¸¡à¸à¸µà¹à¹à¸à¸·à¹à¸­à¹à¸à¹à¹à¸à¸à¸à¹","à¸ªà¸à¸²à¸à¸°","à¸ªà¸à¸²à¸à¸°à¹à¸¥à¸°à¸¥à¹à¸­à¸","à¸ªà¸à¸±à¸à¸ªà¸à¸¸à¸ CheatRunner","à¸ªà¸à¸±à¸à¸ªà¸à¸¸à¸à¸à¸²à¸à¸à¸£à¸²à¸à¸´à¸¥","à¸ªà¸à¸±à¸à¸ªà¸à¸¸à¸à¹à¸à¸£à¹à¸à¸à¸à¹à¸à¸µà¹","à¸à¸¹à¹à¸ªà¸à¸±à¸à¸ªà¸à¸¸à¸","à¸£à¸°à¸à¸","à¸à¹à¸²à¸à¸·à¹à¸à¸à¸²à¸ - à¸à¸µà¸¡à¹à¸¥à¸°à¸ªà¸§à¸´à¸à¸à¹à¹à¸­à¸à¸à¸´à¸à¸à¸µà¸","à¸à¸µà¸¡","à¸à¸µà¸¡à¹à¸¥à¸°à¹à¸­à¸à¸à¸´à¸à¸à¸µà¸","à¹à¸§à¸¥à¸²à¸à¸µà¹à¹à¸«à¸¥à¸·à¸­","à¹à¸§à¸¥à¸²à¹à¸¥à¹à¸","Title ID","à¸ªà¸§à¸´à¸à¸à¹","TouchPad","à¹à¸à¸£à¸à¹à¸à¸­à¸£à¹","Triangle","à¸à¸¶à¹à¸","à¹à¸à¹à¸à¸à¸±à¸à¸à¸¸à¸à¸±à¸","à¹à¸à¹","à¹à¸§à¸­à¸£à¹à¸à¸±à¸","à¸£à¸­à¸«à¸¥à¸±à¸à¹à¸£à¸´à¹à¸¡à¹à¸à¸¡ (à¸¡à¸´à¸¥à¸¥à¸´à¸§à¸´à¸à¸²à¸à¸µ)","à¸à¸³à¹à¸à¸·à¸­à¸à¹à¸¥à¸°à¸à¹à¸­à¸à¸´à¸à¸à¸¥à¸²à¸","à¸ªà¸´à¹à¸à¸à¸µà¹ CheatRunner à¸à¸³à¹à¸à¹à¸¥à¹à¸§ - à¸¡à¸µà¸à¸£à¸°à¹à¸¢à¸à¸à¹à¹à¸¡à¸·à¹à¸­à¸¡à¸µà¸à¸²à¸à¸­à¸¢à¹à¸²à¸à¹à¸¡à¹à¸à¸³à¸à¸²à¸","à¸¡à¸µà¸à¸µà¸","ââ à¹à¸¥à¸·à¹à¸­à¸","à¸à¸²à¸£à¸à¸±à¹à¸à¸à¹à¸²à¸à¸±à¹à¸à¸«à¸¡à¸à¸à¸°à¸à¸¥à¸±à¸à¹à¸à¹à¸à¸à¹à¸²à¹à¸£à¸´à¹à¸¡à¸à¹à¸ à¸à¸­à¸£à¹à¸à¹à¸§à¹à¸à¸à¸°à¸à¸à¹à¸à¸´à¸¡","à¸à¸³à¸¥à¸±à¸à¹à¸à¹à¸à¸µà¸à¸­à¸·à¹à¸à¸­à¸¢à¸¹à¹ à¸¥à¸­à¸à¹à¸«à¸¡à¹à¸­à¸µà¸à¸à¸£à¸¹à¹","à¸à¸¸à¸à¹à¸§à¸­à¸£à¹à¸à¸±à¸","à¹à¸à¹à¸à¹à¸­à¹à¸","à¸à¸·à¸à¸à¹à¸²à¸à¸²à¸£à¹à¸¥à¸·à¸­à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¹à¸¥à¹à¸§","à¸¥à¹à¸²à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¹à¸¥à¹à¸§","à¸à¸±à¸à¸à¸¶à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¹à¸¥à¹à¸§ ({n}) à¸à¸°à¹à¸à¸´à¸à¹à¸¡à¸·à¹à¸­à¹à¸à¸¡à¹à¸£à¸´à¹à¸¡à¸à¸³à¸à¸²à¸","à¸à¸±à¸à¸à¸¶à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¸à¸µà¹à¸§à¹à¸²à¸à¹à¸à¸¥à¹à¸²à¹à¸¥à¹à¸§","à¸à¸µà¸ #{n}","à¹à¸à¸´à¹à¸¡à¹à¸à¸¥à¹à¸à¸µà¸à¹à¸¥à¹à¸§","à¸¥à¸à¹à¸à¸¥à¹à¸à¸µà¸à¹à¸¥à¹à¸§","à¹à¸¥à¸·à¸­à¸à¹à¸à¸¥à¹à¸à¸µà¸à¹à¸¥à¹à¸§","CheatRunner à¸«à¸¢à¸¸à¸à¸à¸­à¸à¸ªà¸à¸­à¸à¸£à¸°à¸«à¸§à¹à¸²à¸à¹à¸à¸´à¸à¹à¸à¸¡","à¸à¸±à¸à¸¥à¸­à¸à¹à¸à¸¥à¹à¸à¸­à¸£à¹à¸à¸µà¸à¹à¸¥à¹à¸§","à¸à¸³à¸¥à¸±à¸à¸à¸£à¸§à¸à¸ªà¸­à¸ {name}","à¸¥à¹à¸²à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´ ({n})","à¸à¸³à¸¥à¸±à¸à¹à¸à¸£à¸µà¸¢à¸à¹à¸à¸µà¸¢à¸à¹à¸¥à¸°à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¹à¸à¸¥à¹à¸à¸µà¸¥à¸°à¹à¸à¸¥à¹","à¸à¸±à¸à¹à¸¢à¹à¸à¸à¸±à¸à¸à¸µà¸à¸­à¸·à¹à¸","à¸à¸±à¸à¸¥à¸­à¸à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸ à¹à¸à¸£à¸à¹à¸¥à¸·à¸­à¸à¸à¹à¸­à¸à¸§à¸²à¸¡à¹à¸­à¸","à¸¥à¹à¸²à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸à¸±à¸à¸¥à¸­à¸à¸à¹à¸­à¸¡à¸¹à¸¥à¸§à¸´à¸à¸´à¸à¸à¸±à¸¢à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸à¸´à¸à¸à¸µà¸à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¹à¸«à¸¥à¸à¹à¸à¸à¸à¹à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¹à¸«à¸¥à¸à¸£à¸²à¸¢à¸à¸·à¹à¸­à¹à¸à¸¡à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸à¸±à¸à¸à¸¶à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸à¸±à¸à¸à¸¶à¸à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸ à¸¥à¸­à¸à¸­à¸µà¸à¸à¸£à¸±à¹à¸","à¹à¸¥à¸·à¸­à¸à¹à¸à¸¥à¹à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¹à¸£à¸´à¹à¸¡à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¹à¸£à¸´à¹à¸¡à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸ªà¸¥à¸±à¸à¸à¸µà¸à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸¥à¸à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸¥à¸à¹à¸à¸¥à¹","à¸¥à¸ {file} à¸«à¸£à¸·à¸­à¹à¸¡à¹","à¸à¸±à¸à¸¥à¸­à¸à¸à¹à¸­à¸¡à¸¹à¸¥à¸§à¸´à¸à¸´à¸à¸à¸±à¸¢à¹à¸¥à¹à¸§ à¸§à¸²à¸à¸¥à¸à¹à¸à¸£à¸²à¸¢à¸à¸²à¸à¸à¸±à¸à¸«à¸²","à¸à¸³à¸¥à¸±à¸à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸ {name}","à¹à¸­à¸à¸à¸´à¸à¸à¸´à¸","à¸à¸¸à¸à¸­à¸¢à¹à¸²à¸à¹à¸à¹à¸à¸à¸±à¸à¸à¸¸à¸à¸±à¸à¹à¸¥à¹à¸§","à¸à¸¢à¸²à¸¢à¹à¸¡à¸à¸¹","à¸à¸³à¸¥à¸±à¸à¸à¸¶à¸à¸à¸±à¹à¸à¸£à¸µà¹à¸à¸ªà¸´à¸à¸­à¸£à¸µà¹à¸à¸à¸£à¸±à¹à¸à¹à¸à¸µà¸¢à¸§","à¹à¸à¸¥à¹ v{a} / à¹à¸à¸¡ v{b}","{name} à¹à¸ªà¸£à¹à¸à¸ªà¸´à¹à¸","à¸à¸±à¸à¸à¸±à¸à¹à¸à¹ {name} à¸«à¸£à¸·à¸­à¹à¸¡à¹","à¹à¸à¸´à¸à¹à¸à¸¡à¹à¸¥à¹à¸§","à¹à¸à¹à¸à¸±à¸à¹à¸à¸¡à¸à¸à¸¥à¸°à¹à¸§à¸­à¸£à¹à¸à¸±à¸ à¸­à¸²à¸à¸à¸³à¹à¸«à¹à¹à¸à¸¡à¹à¸à¸£à¸ à¸à¸²à¸£à¸à¸·à¸à¸à¹à¸²à¸à¸°à¸à¸³à¹à¸à¸à¹à¹à¸à¸´à¸¡à¸à¸¥à¸±à¸à¸¡à¸²","à¹à¸à¸´à¸à¹à¸à¸¡à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¹à¸à¸´à¸à¹à¸à¸¡à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸: {msg}","à¸à¸³à¸à¸­à¹à¸à¸´à¸à¹à¸à¸¡à¸¥à¹à¸¡à¹à¸«à¸¥à¸§ à¹à¸à¸¡à¸­à¸²à¸à¸à¸³à¸¥à¸±à¸à¹à¸£à¸´à¹à¸¡à¸­à¸¢à¸¹à¹","à¸«à¸¡à¸à¹à¸§à¸¥à¸²à¹à¸à¸´à¸à¹à¸à¸¡ à¸à¸£à¸§à¸à¸ªà¸­à¸à¸§à¹à¸²à¹à¸à¸¡à¹à¸£à¸´à¹à¸¡à¹à¸¥à¹à¸§à¸«à¸£à¸·à¸­à¸¢à¸±à¸","à¸à¸³à¸¥à¸±à¸à¹à¸à¸´à¸ {name}...","à¸à¸±à¸à¸¥à¸­à¸à¸à¸±à¸à¸à¸¶à¸à¹à¸¥à¹à¸§","à¸ªà¸£à¹à¸²à¸à¸à¹à¸§à¸¢ {heart} à¹à¸à¸¢ maj0r","à¸à¸£à¸à¸à¸±à¸ v{v}","à¸à¹à¸­à¸à¹à¸à¹ v{v}","à¹à¸¡à¹à¸¡à¸µà¹à¸à¸¡","à¹à¸¡à¹à¸¡à¸µà¹à¸à¸¡à¸à¸µà¹à¸à¸£à¸à¸à¸±à¸à¸à¸³à¸à¹à¸à¸«à¸²","à¸¢à¸±à¸à¹à¸¡à¹à¸¡à¸µà¹à¸à¸¡à¸à¸µà¹à¸¡à¸µà¸à¸µà¸ à¸£à¸±à¸à¹à¸à¹à¸à¸²à¸à¹à¸«à¸¥à¹à¸à¸à¸µà¹à¸¡à¸²","à¸¢à¸±à¸à¹à¸¡à¹à¸¡à¸µà¸à¸±à¸à¸à¸¶à¸","à¹à¸¡à¹à¸¡à¸µà¸à¸£à¸£à¸à¸±à¸à¸à¸µà¹à¸à¸£à¸à¸à¸±à¸","à¸¢à¸±à¸à¹à¸¡à¹à¸à¸£à¹à¸­à¸¡","à¸¢à¸±à¸à¹à¸¡à¹à¸¡à¸µà¸£à¸²à¸¢à¸à¸²à¸£à¸¥à¹à¸²à¸ªà¸¸à¸ à¹à¸à¸´à¸à¹à¸à¸¡à¸«à¸£à¸·à¸­à¹à¸à¸´à¸à¹à¸à¸£à¸à¹à¸à¸­à¸£à¹à¸à¸­à¸à¹à¸à¸¡à¸à¸¹","à¹à¸¡à¹à¸¡à¸µà¸­à¸°à¹à¸£à¹à¸«à¹à¸à¸±à¸à¸¥à¸­à¸","à¹à¸¡à¹à¸¡à¸µà¸­à¸°à¹à¸£à¹à¸à¸´à¸à¸­à¸¢à¸¹à¹","à¹à¸à¸´à¸à¹à¸à¸£à¸à¹à¸à¸­à¸£à¹: {name}","à¸¥à¹à¸²à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¸à¸­à¸à¹à¸à¸à¸à¹à¹à¸¥à¹à¸§","à¸à¸±à¸à¸à¸¶à¸à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¸à¸­à¸à¹à¸à¸à¸à¹à¹à¸¥à¹à¸§","à¹à¸à¹ QR à¹à¸à¹à¸à¹à¸¡à¹à¹à¸à¹","à¸à¸³à¸¥à¸±à¸à¸­à¹à¸²à¸à¸£à¸²à¸¢à¸à¸·à¹à¸­à¹à¸à¸¥à¹à¸à¸²à¸ GitHub","à¸à¸³à¸­à¸­à¸à¸à¸²à¸à¸£à¸²à¸¢à¸à¸²à¸£à¹à¸à¸£à¸","à¸£à¸µà¹à¸à¹à¸à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸£à¸µà¹à¸à¹à¸à¸à¸²à¸£à¸à¸±à¹à¸à¸à¹à¸²à¸«à¸£à¸·à¸­à¹à¸¡à¹","à¸à¸±à¸à¸à¸¶à¸à¹à¸¥à¹à¸§","à¸à¸³à¸¥à¸±à¸à¸à¸±à¸à¸à¸¶à¸à¹à¸à¸¥à¹à¸à¸µà¸","à¸à¸³à¸¥à¸±à¸à¸à¸±à¸à¸à¸¶à¸...","à¸à¸²à¸à¸à¸±à¹à¸à¸ªà¹à¸ CheatRunner.elf à¹à¸«à¸¡à¹à¸à¸²à¸à¸à¸µà¸à¸µà¸à¸­à¸à¸à¸¸à¸","à¸£à¸µà¹à¸à¹à¸à¸à¸²à¸£à¸à¸±à¹à¸à¸à¹à¸²à¹à¸¥à¹à¸§","à¸à¸´à¸","à¸à¸´à¸ CheatRunner à¸«à¸£à¸·à¸­à¹à¸¡à¹","à¹à¸«à¸¥à¹à¸à¸à¸µà¹ {i} à¸à¸²à¸ {n}","à¹à¸£à¸´à¹à¸¡à¹à¸à¸¡à¸à¹à¸­à¸","à¹à¸à¸¡à¸¢à¸±à¸à¹à¸«à¸¥à¸à¸­à¸¢à¸¹à¹","à¹à¸à¸¡à¸¢à¸±à¸à¹à¸£à¸´à¹à¸¡à¹à¸¡à¹à¹à¸ªà¸£à¹à¸ à¸¥à¸­à¸à¸­à¸µà¸à¸à¸£à¸±à¹à¸à¹à¸à¸­à¸µà¸à¹à¸¡à¹à¸à¸µà¹à¸§à¸´à¸à¸²à¸à¸µ","à¸à¸­à¸à¸à¸µà¹à¸¢à¸±à¸à¹à¸à¹à¸£à¸²à¸¢à¸à¸·à¹à¸­à¹à¸à¸¡à¹à¸¡à¹à¹à¸à¹","à¸à¸­à¸à¸à¸µà¹à¸¢à¸±à¸à¸ªà¸¥à¸±à¸à¸à¸µà¸à¸à¸µà¹à¹à¸¡à¹à¹à¸à¹","à¸à¸µà¸à¸à¸µà¹à¹à¸à¸¢à¸à¸³à¹à¸«à¹à¹à¸à¸¡à¹à¸à¸£à¸ à¸¥à¹à¸²à¸à¸à¸²à¸£à¹à¸à¹à¸à¹à¸à¸·à¸­à¸à¹à¸à¸·à¹à¸­à¸¥à¸­à¸à¸­à¸µà¸à¸à¸£à¸±à¹à¸","à¹à¸à¸¥à¹à¸à¸µà¹à¸ªà¸³à¸«à¸£à¸±à¸à¹à¸à¸¡à¸à¸à¸¥à¸°à¹à¸§à¸­à¸£à¹à¸à¸±à¸à¸à¸±à¸à¸à¸µà¹à¸à¸´à¸à¸à¸±à¹à¸à¸­à¸¢à¸¹à¹","à¸à¸²à¸£à¸à¸µà¹à¸à¸°à¸¥à¸à¹à¸à¸¥à¹à¸­à¸­à¸à¸à¸²à¸à¸à¸­à¸à¹à¸à¸¥ à¸à¸¸à¸à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¹à¸«à¸¡à¹à¹à¸à¹à¸à¸²à¸à¹à¸«à¸¥à¹à¸à¸à¸µà¹à¸¡à¸²","à¹à¸à¹à¹à¸§à¸¥à¸² {t}","à¸à¸±à¸à¸¥à¸­à¸ URL à¹à¸¥à¹à¸§","à¸à¸³à¸¥à¸±à¸à¹à¸à¸à¹à¸à¸¥à¹ {name}","à¹à¸à¸à¸à¹à¸à¸µà¹à¹à¸¡à¹à¸£à¸­à¸à¸£à¸±à¸","à¸­à¸±à¸à¹à¸«à¸¥à¸à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¹à¸à¹à¹à¸à¸¥à¹ .mc4, .shn à¸«à¸£à¸·à¸­ .json","à¹à¸à¹à¹à¸§à¸­à¸£à¹à¸à¸±à¸à¸­à¸·à¹à¸à¸«à¸£à¸·à¸­à¹à¸¡à¹","à¹à¸à¹à¸à¹à¸­à¹à¸","à¸à¸³à¸¥à¸±à¸à¸£à¸­ CheatRunner à¸à¸¥à¸±à¸à¸¡à¸²...","à¸à¸±à¸à¸¥à¸­à¸à¸à¸µà¹à¸­à¸¢à¸¹à¹ {label} à¹à¸¥à¹à¸§","à¸à¸¸à¸à¹à¸«à¸¥à¹à¸","à¹à¸à¸¥à¸µà¹à¸¢ {s}","à¹à¸¡à¸·à¹à¸­à¸ªà¸±à¸à¸à¸£à¸¹à¹","à¹à¸à¸¥à¹à¸à¸µà¸","à¸à¸£à¸à¸à¸±à¸ v{v}","à¸à¸´à¸à¸à¸µà¸à¹à¸¥à¹à¸§ {n} à¸£à¸²à¸¢à¸à¸²à¸£","à¸à¸±à¸à¸à¸¶à¸à¹à¸à¸¥à¹à¹à¸«à¸¡à¹à¸«à¸£à¸·à¸­à¸à¸µà¹à¸­à¸±à¸à¹à¸à¸à¹à¸¥à¹à¸§ {n} à¹à¸à¸¥à¹","{n} à¹à¸à¸¡","{n} à¹à¸à¸¡","{n} à¸§à¸±à¸à¸à¹à¸­à¸","{n} à¸à¸¡.à¸à¹à¸­à¸","{n} à¸à¸²à¸à¸µà¸à¹à¸­à¸","{page} - CheatRunner","à¹à¸à¸´à¸à¸­à¸¢à¸¹à¹ {a} à¸à¸²à¸ {n}","{h} à¸à¸¡. {m} à¸à¸²à¸à¸µ","{m} à¸à¸²à¸à¸µ","{s} à¸§à¸´","{m} à¸à¸²à¸à¸µ {s} à¸§à¸´","à¹à¸à¸¢ {a}","à¸¢à¸´à¸à¸à¸µà¸à¹à¸­à¸à¸£à¸±à¸à¸ªà¸¹à¹ CheatRunner","à¸¢à¸±à¸à¹à¸¡à¹à¸¡à¸µà¹à¸à¸¥à¹à¸à¸µà¸ à¸£à¸±à¸à¸à¸µà¸à¸à¸²à¸à¸à¸¸à¸¡à¸à¸à¹à¸à¹à¸à¸µà¹à¹à¸«à¸¥à¹à¸à¸à¸µà¹à¸¡à¸² à¸«à¸£à¸·à¸­à¹à¸à¸´à¸à¹à¸à¸¡à¹à¸¥à¹à¸§à¸§à¸²à¸à¹à¸à¸¥à¹","à¸¢à¸à¹à¸¥à¸´à¸","à¸à¸³à¹à¸à¸´à¸à¸à¸²à¸£à¸à¹à¸­","à¸à¸³à¸¥à¸±à¸à¹à¸à¸·à¹à¸­à¸¡à¸à¹à¸­à¹à¸«à¸¡à¹...","à¸à¸±à¸à¸¥à¸­à¸à¹à¸¥à¹à¸§","à¸à¸±à¸à¸¥à¸­à¸à¹à¸¥à¹à¸§","à¹à¸¡à¹à¸à¸£à¸²à¸","à¸à¸¥à¹à¸­à¸à¹à¸à¹à¸à¸­à¸","à¸à¸´à¸","à¸ªà¸´à¸à¸à¸´à¹","à¸à¸à¸à¸´","à¸à¸²à¸à¸«à¸²à¸¢","à¹à¸¡à¹à¸ªà¸³à¹à¸£à¹à¸","à¸à¸³à¸¥à¸±à¸à¹à¸à¸´à¸","à¸¥à¸­à¸à¸­à¸µà¸à¸à¸£à¸±à¹à¸","à¹à¸à¸¡","à¸à¹à¸à¸«à¸²à¸à¸µà¸","à¸à¸´à¸à¸à¸²à¸¡ maj0r à¸à¸ X","à¸à¸´à¸à¸à¸²à¸¡ maj0r à¸à¸ Bluesky","à¸ªà¸£à¹à¸²à¸à¹à¸à¸¢ maj0r - ð§ð·","à¸à¸²à¸à¸à¸à¸à¸²à¸§à¹à¸«à¹à¹à¸à¸£à¹à¸à¸à¸à¹à¸à¹à¸§à¸¢! â­","à¹à¸à¸à¹à¸à¹à¸¡","à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´","à¸£à¸µà¹à¸à¹à¸","à¹à¸¡à¹à¸¡à¸µà¹à¸à¸à¸à¹à¸ªà¸³à¸«à¸£à¸±à¸à¹à¸à¸¡à¸à¸µà¹","à¸§à¸²à¸à¹à¸à¸à¸à¹ XML à¸à¸­à¸à¸à¸¸à¸à¹à¸ /data/cheatrunner/patches/xml","à¸¡à¸µà¸­à¸±à¸à¹à¸à¸à¹à¸«à¸¡à¹","à¸¡à¸µà¹à¸§à¸­à¸£à¹à¸à¸±à¸ {v} à¹à¸¥à¹à¸§","à¹à¸£à¸µà¸¢à¸à¸à¸¹à¸ªà¸¹à¸à¸£à¹à¸à¸à¸­à¸­à¸à¹à¸¥à¸à¹","à¸à¹à¸à¸«à¸²à¹à¸à¸¡à¸à¸µà¹à¸¡à¸µà¸ªà¸¹à¸à¸£à¹à¸à¸à¸à¸±à¹à¸à¸«à¸¡à¸à¹à¸¥à¸°à¸à¸£à¸§à¸à¸ªà¸­à¸à¸§à¹à¸²à¹à¸à¸¡à¸à¸­à¸à¸à¸¸à¸à¸¡à¸µà¸«à¸£à¸·à¸­à¹à¸¡à¹","à¹à¸à¸´à¸à¹à¸§à¹à¸à¹à¸à¸à¹","à¸à¸¥à¸±à¸à¹à¸à¸à¸à¹à¸à¸²à¸à¸à¸¸à¸¡à¸à¸","à¹à¸à¸à¸à¹à¸ªà¸³à¹à¸£à¹à¸à¸£à¸¹à¸à¸ªà¸³à¸«à¸£à¸±à¸à¹à¸à¸¡à¸à¸­à¸à¸à¸¸à¸ à¹à¸¡à¹à¸¡à¸µà¸à¸²à¸£à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸ à¸à¸¥à¸±à¸à¸à¸°à¹à¸à¸´à¸à¹à¸à¹à¸à¹à¸à¹à¸«à¸¡à¹","à¹à¸à¸´à¸ PS-Game-Patch","à¹à¸­à¸à¹à¸à¸à¸à¹à¸ à¸²à¸","à¹à¸«à¸¡à¸à¸¥à¸à¸à¸°à¸à¸´à¸à¸à¸²à¸£à¹à¸à¸¥à¸­ à¹à¸à¸² à¹à¸¥à¸°à¹à¸­à¸à¸´à¹à¸¡à¸à¸±à¸ à¸à¸¶à¹à¸à¹à¸à¹à¸à¸à¹à¸²à¹à¸£à¸´à¹à¸¡à¸à¹à¸à¹à¸à¹à¸à¸£à¸²à¸§à¹à¹à¸à¸­à¸£à¹à¸à¸­à¸ PS5","à¹à¸à¹à¸¡","à¸¥à¸","à¹à¸ªà¸à¸à¹à¸à¸´à¹à¸¡à¹à¸à¸´à¸¡ ({n})","à¹à¸à¸°à¸à¸³","à¸£à¸µà¸¥à¸µà¸ªà¸à¸²à¸à¸à¸²à¸£à¸à¸­à¸ PS-Game-Patch à¹à¸à¸à¸à¹à¸à¸°à¸à¸¹à¸à¸à¸±à¸à¸à¸¶à¸à¹à¸§à¹à¸à¸µà¹ /data/cheatrunner/patches","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¹à¸à¸à¸à¹","à¸à¸³à¸¥à¸±à¸à¸à¸±à¸à¸à¸¶à¸à¹à¸à¸¥à¹à¹à¸à¸à¸à¹","à¹à¸à¸à¸à¹à¸à¸²à¸à¸à¸±à¸§à¸à¸°à¹à¸à¹à¸à¸²à¸à¹à¸à¹à¸à¹à¸à¹à¸­à¹à¸¡à¸·à¹à¸­à¹à¸à¹à¸à¸­à¸à¹à¸£à¸´à¹à¸¡à¹à¸à¸¡: à¸à¸±à¸à¸à¸¶à¸à¸à¸²à¸£à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´à¹à¸¥à¹à¸§à¹à¸£à¸´à¹à¸¡à¹à¸à¸¡à¹à¸«à¸¡à¹","à¸à¸²à¸£à¹à¸à¹à¹à¸à¸à¸±à¸à¸«à¸²","à¸£à¸²à¸¢à¸à¸²à¸à¸à¸±à¸à¸«à¸² à¸«à¸£à¸·à¸­à¸à¸·à¸à¸à¹à¸²à¸à¸²à¸£à¸à¸±à¹à¸à¸à¹à¸²à¸à¸±à¹à¸à¸«à¸¡à¸à¹à¸à¹à¸à¸à¹à¸²à¹à¸£à¸´à¹à¸¡à¸à¹à¸","à¸à¸±à¸à¸¥à¸­à¸à¸à¹à¸­à¸¡à¸¹à¸¥à¸§à¸´à¸à¸´à¸à¸à¸±à¸¢à¹à¸¥à¸°à¹à¸à¸´à¸à¸«à¸à¹à¸² Issue à¸à¸ GitHub","à¸à¸°à¹à¸¡à¹à¹à¸à¸°à¸à¹à¸­à¸à¹à¸à¸ à¹à¸à¸à¸à¹ à¹à¸¥à¸°à¹à¸à¸£à¹à¸à¸¥à¹à¹à¸«à¸¥à¸à¸­à¸±à¸à¹à¸à¸¡à¸±à¸à¸´","à¸à¸¸à¸à¸­à¸¢à¹à¸²à¸à¸à¸à¸à¸´à¸à¸µ","à¸¡à¸µà¸à¸²à¸à¸­à¸¢à¹à¸²à¸à¸à¸µà¹à¸à¹à¸­à¸à¸à¸£à¸§à¸à¸ªà¸­à¸ - à¸à¸¹à¸£à¸²à¸¢à¸¥à¸°à¹à¸­à¸µà¸¢à¸à¸à¹à¸²à¸à¸¥à¹à¸²à¸","à¹à¸à¸¥à¹ .mc4, .shn à¸«à¸£à¸·à¸­ .json à¸à¸²à¸à¸à¸­à¸¡à¸à¸´à¸§à¹à¸à¸­à¸£à¹à¸à¸­à¸à¸à¸¸à¸","à¹à¸à¸´à¹à¸¡à¹à¸à¹à¸à¸µà¹à¹à¸à¸¥à¹à¸«à¸£à¸·à¸­à¹à¸«à¸¥à¹à¸à¸à¸µà¹à¸¡à¸²","à¸£à¸¹à¸à¸¥à¸±à¸à¸©à¸à¹","à¸£à¸¹à¸à¸¥à¸±à¸à¸©à¸à¹ à¹à¸­à¸à¸à¸´à¸à¸à¸µà¸ à¹à¸¥à¸°à¸à¸±à¸§à¹à¸¥à¸·à¸­à¸à¸à¸­à¸¢ à¸à¸²à¸£à¹à¸à¸¥à¸µà¹à¸¢à¸à¹à¸à¸¥à¸à¸à¸°à¸à¸±à¸à¸à¸¶à¸à¸à¸±à¸à¸à¸µ","à¹à¸à¸¥à¹à¸à¸µà¸","à¹à¸à¸¥à¹à¸à¸µà¸à¸à¸­à¸à¹à¸à¸¡à¸à¸µà¹","CheatRunner à¹à¸ªà¸à¸à¹à¸à¸¡à¸à¸µà¹à¸à¸´à¸à¸à¸±à¹à¸à¹à¸à¸à¸­à¸à¹à¸à¸¥à¸à¸µà¹ à¸à¸´à¸à¸à¸±à¹à¸à¹à¸à¸¡à¹à¸¥à¹à¸§à¸£à¸µà¹à¸à¸£à¸à¸£à¸²à¸¢à¸à¸²à¸£","à¸à¹à¸­à¸à¹à¸à¸´à¸à¸à¹à¸­à¸à¸à¸¶à¸à¸à¸°à¹à¸à¹à¸à¸µà¸à¹à¸¥à¸°à¹à¸à¸à¸à¹à¹à¸à¹","à¸à¸³à¸¥à¸±à¸à¸à¸£à¸§à¸à¸ªà¸­à¸...","à¹à¸¥à¸·à¸­à¸à¹à¸à¸¥à¹à¸­à¸·à¹à¸","à¹à¸«à¸¥à¸à¹à¸¡à¹à¹à¸à¹","à¹à¸«à¸¥à¸à¹à¸à¸¥à¹à¸à¸µà¸à¹à¸¡à¹à¹à¸à¹","à¹à¸«à¸¥à¸à¹à¸à¸à¸à¹à¹à¸¡à¹à¹à¸à¹","à¹à¸«à¸¥à¸à¸à¸²à¸£à¸à¸±à¹à¸à¸à¹à¸²à¹à¸¡à¹à¹à¸à¹","à¹à¸«à¸¥à¸à¹à¸à¸¡à¸à¸­à¸à¸à¸¸à¸à¹à¸¡à¹à¹à¸à¹","à¸­à¹à¸²à¸à¸ªà¸à¸²à¸à¸°à¸à¸µà¸à¸à¸±à¸à¸à¸¸à¸à¸±à¸à¹à¸¡à¹à¹à¸à¹","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸µà¸à¸à¸²à¸à¸à¸¸à¸¡à¸à¸à¸à¸µà¹à¹à¸«à¸¥à¹à¸à¸à¸µà¹à¸¡à¸² à¸«à¸£à¸·à¸­à¹à¸à¸´à¹à¸¡à¹à¸à¸¥à¹à¸à¸²à¸à¸à¸­à¸¡à¸à¸´à¸§à¹à¸à¸­à¸£à¹à¸à¸­à¸à¸à¸¸à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸µà¹à¹à¸«à¸¥à¹à¸à¸à¸µà¹à¸¡à¸² à¸«à¸£à¸·à¸­à¸§à¸²à¸à¹à¸à¸¥à¹à¸à¹à¸²à¸à¸¥à¹à¸²à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸µà¸à¹à¸¥à¸°à¹à¸à¸à¸à¹à¸ªà¸³à¹à¸£à¹à¸à¸£à¸¹à¸à¸ªà¸³à¸«à¸£à¸±à¸à¹à¸à¸¡à¸à¸­à¸à¸à¸¸à¸ à¸à¸²à¸£à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸°à¸à¸³à¸à¸²à¸à¸à¹à¸­à¸à¸à¸°à¸à¸µà¹à¸à¸¸à¸à¹à¸à¹à¸à¸²à¸à¸«à¸à¹à¸²à¸­à¸·à¹à¸","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸à¸à¸¸à¸à¹à¸à¸à¸à¹à¸à¸²à¸à¸à¸²à¸£à¸à¸µà¹à¹à¸«à¸¥à¹à¸à¸à¸µà¹à¸¡à¸² à¸«à¸£à¸·à¸­à¸§à¸²à¸à¹à¸à¸à¸à¹ XML à¸à¸­à¸à¸à¸¸à¸à¹à¸ /data/cheatrunner/patches/xml","à¸à¸³à¸¥à¸±à¸à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸","à¸à¸³à¸¥à¸±à¸à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸ {p}%","à¸à¸²à¸§à¸à¹à¹à¸«à¸¥à¸","à¹à¸­à¸à¸à¸´à¸","à¸à¸²à¸¡à¸à¸£à¸£à¸à¸±à¸à¸¥à¹à¸²à¸ªà¸¸à¸","à¸£à¸±à¸à¸à¸µà¸","à¸ªà¸à¸²à¸à¸°","à¸à¸¥à¸±à¸à¹à¸à¸¡","à¸ªà¸à¸²à¸à¸°à¸ªà¸à¸à¸­à¸à¹à¸­à¸à¸à¸´à¸à¸à¸µà¸à¹à¸¥à¸°à¸à¸²à¸£à¹à¸à¸à¸à¹à¸«à¸à¹à¸§à¸¢à¸à¸§à¸²à¸¡à¸à¸³ à¹à¸¥à¸°à¸ªà¸´à¹à¸à¸à¸µà¹ CheatRunner à¸à¸³à¹à¸","à¸à¹à¸²à¸§à¸ªà¸²à¸£ à¸à¸§à¸²à¸¡à¸à¹à¸§à¸¢à¹à¸«à¸¥à¸·à¸­ à¹à¸¥à¸°à¸à¸¹à¹à¸­à¸¢à¸¹à¹à¹à¸à¸·à¹à¸­à¸à¸«à¸¥à¸±à¸ CheatRunner","à¹à¸à¸¡à¸à¸µà¹à¸¢à¸±à¸à¹à¸¡à¹à¸¡à¸µà¹à¸à¸¥à¹à¸à¸µà¸","à¹à¸¡à¹à¸à¸à¸à¸µà¸à¸à¸µà¹à¸à¸£à¸à¸à¸±à¸ \"{q}\"","à¸¢à¸±à¸à¹à¸¡à¹à¸¡à¸µà¹à¸à¸¥à¹","à¸¢à¸±à¸à¹à¸¡à¹à¸à¸à¹à¸à¸¡","à¹à¸¡à¹à¸à¸à¹à¸à¸¥à¹à¹à¸à¹à¸à¸£à¸·à¹à¸­à¸","à¹à¸à¸¡à¸à¸µà¹à¹à¸¡à¹à¸¡à¸µà¹à¸à¸à¸à¹","à¸à¸³à¸¥à¸±à¸à¹à¸¥à¹à¸à¸­à¸¢à¸¹à¹","à¹à¸à¸´à¸à¸à¸²à¸£à¸à¸±à¹à¸à¸à¹à¸²","à¸«à¸¢à¸¸à¸à¸à¸±à¹à¸§à¸à¸£à¸²à¸§à¸à¸à¸°à¸à¸µà¹à¸à¸¸à¸à¸­à¹à¸²à¸","à¸à¸²à¸£à¸à¸³à¸«à¸à¸à¸à¹à¸²","à¹à¸«à¸¡à¸à¸¥à¸à¸à¸°à¹à¸­à¸²à¹à¸­à¸à¸´à¹à¸¡à¸à¸±à¸à¸à¸±à¹à¸à¸«à¸¡à¸à¸­à¸­à¸ à¹à¸à¹à¸à¸à¹à¸²à¹à¸£à¸´à¹à¸¡à¸à¹à¸à¹à¸à¹à¸à¸£à¸²à¸§à¹à¹à¸à¸­à¸£à¹à¸à¸­à¸ PS5","à¸à¸±à¸à¸à¸¶à¸à¹à¸§à¹à¸à¸µà¹ /data/cheatrunner/cheats à¹à¸à¸¥à¹à¸à¸µà¹à¸¡à¸µà¸à¹à¸²à¸¢ \"à¸à¸³à¸¥à¸±à¸à¹à¸à¹à¸à¸²à¸\" à¸à¸·à¸­à¹à¸à¸¥à¹à¸à¸µà¹à¹à¸à¸£à¸à¹à¸à¸­à¸£à¹à¸­à¹à¸²à¸","à¸ªà¹à¸à¸£à¸²à¸¢à¸à¸²à¸à¹à¸¡à¸·à¹à¸­à¸¡à¸µà¸à¸²à¸à¸­à¸¢à¹à¸²à¸à¸à¸³à¸à¸²à¸à¹à¸¡à¹à¸à¸¹à¸à¸à¹à¸­à¸","à¹à¸à¸´à¸à¹à¸à¸¡à¹à¸à¸·à¹à¸­à¹à¸à¹à¸à¸µà¸","à¹à¸à¸´à¸à¹à¸à¸¡à¸à¸µà¹à¹à¸à¸·à¹à¸­à¹à¸à¹à¸à¸µà¸à¸à¸­à¸à¹à¸à¸¡","à¸£à¸²à¸¢à¸¥à¸°à¹à¸­à¸µà¸¢à¸à¸à¸²à¸à¹à¸à¸à¸à¸´à¸","à¹à¸­à¸à¸à¸´à¸à¸à¸µà¸à¸à¸´à¸à¸­à¸¢à¸¹à¹","à¸­à¹à¸²à¸à¸à¹à¸²à¸à¸±à¸à¸à¸¸à¸à¸±à¸à¹à¸¡à¹à¹à¸à¹ à¸à¸¶à¸à¹à¸¡à¹à¹à¸ªà¸à¸à¸­à¸°à¹à¸£à¹à¸à¸à¸à¸µà¹à¸à¸°à¹à¸ªà¸à¸à¸à¹à¸²à¸à¸µà¹à¸à¸´à¸","à¸ªà¸´à¹à¸à¸à¸³à¹à¸à¹à¸à¸ªà¸³à¸«à¸£à¸±à¸à¸à¸²à¸£à¹à¸à¹à¸à¸µà¸à¹à¸¥à¸°à¹à¸à¸à¸à¹","à¸¡à¸µà¹à¸à¸¥à¹à¸­à¸¢à¸¹à¹à¹à¸à¹à¸­à¹à¸²à¸à¹à¸¡à¹à¹à¸à¹ à¸­à¸²à¸à¹à¸ªà¸µà¸¢à¸«à¸²à¸¢à¸«à¸£à¸·à¸­à¸¢à¸±à¸à¸à¸±à¸à¸¥à¸­à¸à¹à¸¡à¹à¹à¸ªà¸£à¹à¸","à¹à¸«à¸¥à¸à¹à¸à¸¥à¹à¹à¸à¹à¹à¸à¹à¸§à¹à¸²à¸à¹à¸à¸¥à¹à¸² à¸¥à¸­à¸à¹à¸à¸¥à¹à¸­à¸·à¹à¸à¸ªà¸³à¸«à¸£à¸±à¸à¹à¸à¸¡à¸à¸µà¹","à¸à¸­à¸à¸à¸µà¹à¸¢à¸±à¸à¹à¸à¹à¸£à¸²à¸¢à¸à¸²à¸£à¹à¸à¸¡à¹à¸¡à¹à¹à¸à¹ à¸à¸£à¸§à¸à¸ªà¸­à¸à¸§à¹à¸²à¸à¸­à¸à¹à¸à¸¥à¹à¸à¸´à¸à¸­à¸¢à¸¹à¹ à¹à¸¥à¹à¸§à¸¥à¸­à¸à¸­à¸µà¸à¸à¸£à¸±à¹à¸","à¸­à¹à¸²à¸à¹à¸à¸¥à¹à¹à¸à¸à¸à¹à¸à¸­à¸à¹à¸à¸¡à¸à¸µà¹à¹à¸¡à¹à¹à¸à¹ à¸¥à¸­à¸à¸­à¸µà¸à¸à¸£à¸±à¹à¸à¹à¸à¸­à¸µà¸à¸ªà¸±à¸à¸à¸£à¸¹à¹","à¸ªà¸à¸²à¸à¸°à¸ªà¸§à¸´à¸à¸à¹à¸­à¸²à¸à¹à¸¡à¹à¹à¸à¹à¸à¸à¸±à¸à¸à¸¸à¸à¸±à¸","à¸à¸µà¸¡ à¹à¸­à¸à¹à¸à¸à¸à¹ à¹à¸¥à¸°à¸ à¸²à¸©à¸²à¸à¸­à¸à¹à¸à¸à¸à¸­à¸£à¹à¸à¸à¸µà¹","à¹à¸à¸¥à¹à¸à¸µà¸à¸à¸µà¹à¹à¸¡à¹à¸¡à¸µà¸à¸µà¸","à¸¥à¸­à¸à¸­à¸µà¸à¸à¸£à¸±à¹à¸à¸à¹à¸²à¸à¸¥à¹à¸²à¸","à¹à¸à¸´à¸à¹à¸à¹à¸à¸µà¹à¸à¸²à¸£à¸à¸±à¹à¸à¸à¹à¸²","à¹à¸à¸à¸­à¸±à¸à¸à¸£à¸²à¸¢","à¹à¸à¸à¸µà¹à¸¥à¹à¸²à¸ªà¸¸à¸","à¸¥à¸à¹à¸à¸¥à¹à¸à¸µà¹à¹à¸à¹à¸­à¸¢à¸¹à¹","à¹à¸«à¸¥à¹à¸à¸à¸µà¸","à¸à¸£à¸­à¸à¹à¸à¸¡","à¸à¸¹à¹à¸à¸¡à¸à¸­à¸à¸à¸¸à¸","à¸à¸¹à¹à¸à¸±à¸à¸à¸³","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"tr-TR":["Bir hile geÃ§en sefer oyunu Ã§Ã¶kertti.","HakkÄ±nda","Eylem","Eylemler","Dosya ekle","Hile ekle","Favorilere ekle","Her oyunun seÃ§enekler menÃ¼sÃ¼ne bir â CheatRunner giriÅi ve Ã§alÄ±Åan oyunun trainer'Ä±nÄ± aÃ§an bir tuÅ kombinasyonu ekler.","GeliÅmiÅ","TÃ¼mÃ¼","TÃ¼m katkÄ±lar, araÅtÄ±rma, geliÅtirme ve gelecekteki yÃ¼kseltmeler iÃ§in dÃ¼ÅÃ¼k firmware'li (1.xxâ4.xx) bir PS5 edinme hedefimize gidecek.","TÃ¼m indirmeler baÅarÄ±sÄ±z oldu","TÃ¼m oyunlar","DeÄiÅen dosyalarÄ± da gÃ¼ncelle","UygulandÄ±","Uygula","Uygulamalar","Otomatik seÃ§","Geri","KÃ¼tÃ¼phanene gÃ¶z at","TuÅlar","Buy Me a Coffee","Hile","Hile motoru","CheatRunner - PS5'in iÃ§in hile trainer'Ä±!","Hileler","Hileler, oyun baÅladÄ±ktan sonra bu sÃ¼re boyunca bekletilir.","Ãnce hileliler","Bir aÄ seÃ§in","Circle","Filtreleri temizle","Ä°Åareti temizle","AramayÄ± temizle","Kapat","Daralt","MenÃ¼yÃ¼ daralt","Bu panonun renk ÅemasÄ±.","Topluluk","Topluluk hile depolarÄ±","Kumanda kÄ±sayolu ve oyun menÃ¼sÃ¼","Kopyala","URL'yi kopyala","TÃ¼mÃ¼nÃ¼ kopyala","Hile klasÃ¶rÃ¼nÃ¼ kopyala","BaÄlantÄ±yÄ± kopyala","ÃÃ¶kme algÄ±lama","GeÃ§en sefer oyunu Ã§Ã¶kertti","Cross","Kripto","Kripto desteÄi","Koyu","Sil","TÃ¼mÃ¼nÃ¼ kapat","AÅaÄÄ±","TÃ¼m kaynaklarÄ± indir","Topluluk hilelerini indirin","Ä°ndirme baÅarÄ±sÄ±z","OyunlarÄ±n iÃ§in hazÄ±r hile dosyalarÄ±nÄ± indir. /data/cheatrunner/cheats klasÃ¶rÃ¼ne kaydedilir ve indirmeler arka planda devam eder.","SeÃ§ileni indir","Ä°ndirildi","Buraya bir .mc4, .shn veya .json dosyasÄ± bÄ±rakÄ±n ya da gÃ¶z atmak iÃ§in tÄ±klayÄ±n","Kumanda kÄ±sayolunu etkinleÅtir","KÄ±sayolu etkinleÅtir","Motor aÃ§Ä±k","Enter ile aÃ§","YalnÄ±zca hatalar","Tam","BaÅarÄ±sÄ±z","Favoriler","Dosyalar","Filtrele","GÃ¼nlÃ¼kleri filtrele","Oyun hile aÃ§Ä±ldÄ±ktan hemen sonra kapanÄ±rsa hileyi iÅaretle.","KullanÄ±cÄ±lar iÃ§in. KullanÄ±cÄ±lar tarafÄ±ndan.","Zorla uygula","Oyunlar","Hileli oyunlar","YamalÄ± oyunlar","Hileler arasÄ± bekleme (ms)","Genel","KÄ±sayolu tetiklemek iÃ§in birlikte basÄ±lÄ± tutulur.","BasÄ±lÄ± tutma sÃ¼resi (ms)","ÃalÄ±Åan oyunun trainer'Ä±nÄ± aÃ§mak iÃ§in iki tuÅa birlikte basÄ±lÄ± tutun.","Ana ekran kutucuÄu","Sistem arayÃ¼zÃ¼ne baÄlanÄ±r. VarsayÄ±lan olarak kapalÄ± - bazÄ± firmware'lerde ana ekranÄ± Ã§Ã¶kertebilir.","Kombinasyonun tetiklenmeden Ã¶nce ne kadar basÄ±lÄ± tutulacaÄÄ±.","GÃ¼nlÃ¼Äe ne kadar ayrÄ±ntÄ± yazÄ±lacaÄÄ±.","CheatRunner iÅine yaradÄ±ysa projenin geliÅtirilmesine destek olmayÄ± dÃ¼ÅÃ¼nÃ¼n! ð«¶","KullanÄ±mda","AÃ§Ä±lÄ±Åta CheatRunner kutucuÄunu PS5 ana ekranÄ±na kurar.","Discord'a katÄ±l","Ko-Fi, kahve, kripto, PIX","Dil","Bu panonun dili.","Son oynama","Sol","AÃ§Ä±k","Hile motorunun ve bellek yamalamanÄ±n durumu.","Yerel hile dosyalarÄ±","GÃ¼nlÃ¼k dÃ¼zeyi","GÃ¼nlÃ¼k dÃ¼zeyi filtresi","GÃ¼nlÃ¼kler","Ana","Hile ve yamalarÄ± uygulamak iÃ§in ana anahtar.","Ana kod dÃ¼zeltmesi","Bu oyun sÃ¼rÃ¼mÃ¼yle uyuÅmayabilir","Bellek yamalama","Gece yarÄ±sÄ±","Ä°ki hile geÃ§iÅi arasÄ±ndaki asgari sÃ¼re.","Ad A-Z","Ad Z-A","Bu oyun iÃ§in henÃ¼z hile dosyasÄ± yok.","HenÃ¼z favori yok. Eklemek iÃ§in bir oyunun yÄ±ldÄ±zÄ±na dokunun.","ÃalÄ±ÅmÄ±yor","AÃ§Ä±k","Telefonda aÃ§","Telefonda aÃ§ (QR kod)","Panoyu telefonunda aÃ§","Trainer'Ä± aÃ§","SeÃ§enekler","BaÅka sÃ¼rÃ¼m","Sayfa","Yamalar","Oyna","QR kod","Pano adresi iÃ§in QR kod","Bu oyun her aÃ§Ä±ldÄ±ÄÄ±nda AÃIK hileleri yeniden uygula","HazÄ±r","Son kullanÄ±lanlar","Yenile","Oyun listesini yenile","Yeniden baÅlat","Sorun bildir","YÃ¼klÃ¼ oyunlarÄ± yeniden tara","VarsayÄ±lanlara sÄ±fÄ±rla","Ana koda baÄlÄ± hileleri otomatik Ã§Ã¶z.","Geri yÃ¼kle","SaÄ","ÃalÄ±ÅÄ±yor","Åu an Ã§alÄ±ÅÄ±yor","Otomatik yÃ¼klemeyi kaydet","Telefon kamerasÄ±yla tarayÄ±n. Telefonunuz PS5 ile aynÄ± Wi-Fi aÄÄ±nda olmalÄ±.","Ara","Ada veya baÅlÄ±k kimliÄine gÃ¶re ara","Oyun ara","Oyun, sayfa ve eylem ara","TamamÄ±nÄ± kopyalamak iÃ§in bir adres seÃ§in.","Ayarlar","QR kodu gÃ¶ster","Payload'u kapat","OyunlarÄ± sÄ±rala","Kaynak","Kaynaklar","HÄ±z","Square","Oyunu baÅlatÄ±n","YamalarÄ± uygulamak iÃ§in bu oyunu baÅlatÄ±n.","Durum","Durum ve gÃ¼nlÃ¼kler","CheatRunner'a destek ol","Brezilya'dan destek","Bu projeyi destekle","DestekÃ§iler","Sistem","Temel ayarlar - tema ve hile motoru anahtarÄ±.","Tema","Tema ve hile motoru","Kalan sÃ¼re","Oynama sÃ¼resi","BaÅlÄ±k kimliÄi","Anahtarlar","TouchPad","Trainer","Triangle","YukarÄ±","GÃ¼ncel","Kullan","SÃ¼rÃ¼m","Oyun baÅladÄ±ktan sonra bekleme (ms)","UyarÄ±lar ve hatalar","CheatRunner'Ä±n neler yaptÄ±ÄÄ± - bir Åey Ã§alÄ±ÅmadÄ±ÄÄ±nda iÅe yarar.","Hileli","ââ gezin","TÃ¼m ayarlar varsayÄ±lanlara dÃ¶ner. Web portu korunur.","BaÅka bir hile uygulanÄ±yor. Birazdan tekrar deneyin.","Herhangi bir sÃ¼rÃ¼m","Yine de uygula","Otomatik seÃ§im geri yÃ¼klendi.","Otomatik yÃ¼kleme temizlendi.","Otomatik yÃ¼kleme kaydedildi ({n}). Oyun baÅlayÄ±nca aÃ§Ä±lÄ±rlar.","BoÅ otomatik yÃ¼kleme kaydedildi.","Hile #{n}","Hile dosyasÄ± eklendi.","Hile dosyasÄ± silindi.","Hile dosyasÄ± seÃ§ildi.","CheatRunner baÅlatma sÄ±rasÄ±nda yanÄ±t vermeyi bÄ±raktÄ±.","Hile klasÃ¶rÃ¼ kopyalandÄ±.","{name} denetleniyor","Otomatik yÃ¼klemeyi temizle ({n})","Dosyalar tek tek karÅÄ±laÅtÄ±rÄ±lÄ±p indiriliyor","BaÅka bir hileyle Ã§akÄ±ÅÄ±yor","KopyalanamadÄ±. Metni elle seÃ§in.","Otomatik yÃ¼kleme temizlenemedi.","TanÄ±lama kopyalanamadÄ±.","Hileler kapatÄ±lamadÄ±.","Yamalar yÃ¼klenemedi.","Oyun listesi yÃ¼klenemedi.","Otomatik yÃ¼kleme kaydedilemedi.","Kaydedilemedi. Tekrar deneyin.","Dosya seÃ§ilemedi.","Ä°ndirme baÅlatÄ±lamadÄ±.","BaÅlatÄ±lamadÄ±.","Hile deÄiÅtirilemedi.","Silinemedi.","DosyayÄ± sil","{file} silinsin mi?","TanÄ±lama kopyalandÄ±. Sorun bildirimine yapÄ±ÅtÄ±rÄ±n.","{name} indiriliyor","Motor kapalÄ±","Her Åey zaten gÃ¼ncel","MenÃ¼yÃ¼ geniÅlet","TÃ¼m depo tek seferde alÄ±nÄ±yor","Dosya v{a} / oyun v{b}","{name} tamamlandÄ±","{name} zorla uygulansÄ±n mÄ±?","Oyun baÅlatÄ±ldÄ±.","BaÅka bir oyun sÃ¼rÃ¼mÃ¼ iÃ§in hazÄ±rlanmÄ±Å, oyunu Ã§Ã¶kertebilir. Geri yÃ¼kle orijinal baytlarÄ± geri koyar.","BaÅlatma baÅarÄ±sÄ±z.","BaÅlatma baÅarÄ±sÄ±z: {msg}","BaÅlatma isteÄi baÅarÄ±sÄ±z. Oyun hÃ¢lÃ¢ baÅlÄ±yor olabilir.","BaÅlatma zaman aÅÄ±mÄ±na uÄradÄ±. Oyunun aÃ§Ä±lÄ±p aÃ§Ä±lmadÄ±ÄÄ±nÄ± kontrol edin.","{name} baÅlatÄ±lÄ±yor...","GÃ¼nlÃ¼kler kopyalandÄ±.","maj0r tarafÄ±ndan {heart} ile yapÄ±ldÄ±","v{v} ile eÅleÅiyor","v{v} gerekli","Oyun yok","AramanÄ±zla eÅleÅen oyun yok.","HenÃ¼z hileli oyun yok. Kaynaklar'dan edinin.","HenÃ¼z gÃ¼nlÃ¼k yok.","EÅleÅen satÄ±r yok.","HazÄ±r deÄil","YakÄ±n zamanda bir Åey yok. Bir oyun baÅlatÄ±n ya da trainer'Ä±nÄ± aÃ§Ä±n.","Kopyalanacak bir Åey yok.","AÃ§Ä±k olan bir Åey yoktu.","Trainer'Ä± aÃ§: {name}","Yama otomatik yÃ¼klemesi temizlendi.","Yama otomatik yÃ¼klemesi kaydedildi.","QR kod kullanÄ±lamÄ±yor.","Dosya listesi GitHub'dan okunuyor","Favorilerden kaldÄ±r","SÄ±fÄ±rlama baÅarÄ±sÄ±z.","Ayarlar sÄ±fÄ±rlansÄ±n mÄ±?","Kaydedildi","Hile dosyalarÄ± kaydediliyor","Kaydediliyor...","ArdÄ±ndan PC'nizden yeni bir CheatRunner.elf gÃ¶nderin.","Ayarlar sÄ±fÄ±rlandÄ±.","Kapat","CheatRunner kapatÄ±lsÄ±n mÄ±?","Kaynak {i}/{n}","Ãnce oyunu baÅlatÄ±n.","Oyun hÃ¢lÃ¢ yÃ¼kleniyor.","Oyun hÃ¢lÃ¢ baÅlÄ±yor. BirkaÃ§ saniye sonra tekrar deneyin.","Oyun listesi Åu anda kullanÄ±lamÄ±yor.","Bu hile Åu anda deÄiÅtirilemiyor.","Bu hile daha Ã¶nce oyunu Ã§Ã¶kertti. Tekrar denemek iÃ§in iÅareti temizleyin.","Bu dosya, yÃ¼klÃ¼ olandan farklÄ± bir oyun sÃ¼rÃ¼mÃ¼ iÃ§indir.","Bu iÅlem dosyayÄ± konsoldan kaldÄ±rÄ±r. Kaynaklar'dan yeniden indirebilirsiniz.","SÃ¼re: {t}","URL kopyalandÄ±.","{name} aÃ§Ä±lÄ±yor","Desteklenmeyen yama","YÃ¼kleme baÅarÄ±sÄ±z.","Bir .mc4, .shn veya .json dosyasÄ± kullanÄ±n.","BaÅka sÃ¼rÃ¼m kullanÄ±lsÄ±n mÄ±?","Yine de kullan","CheatRunner'Ä±n geri gelmesi bekleniyor...","{label} adresi kopyalandÄ±","tÃ¼m kaynaklar","ort. {s}","az Ã¶nce","hile dosyasÄ±","v{v} uyuÅuyor","{n} hile kapatÄ±ldÄ±.","{n} yeni veya gÃ¼ncellenen dosya kaydedildi","{n} oyun","{n} oyun","{n} gÃ¼n Ã¶nce","{n} sa Ã¶nce","{n} dk Ã¶nce","{page} - CheatRunner","{n} iÃ§inden {a} aÃ§Ä±k","{h} sa {m} dk","{m} dk","{s} sn","{m} dk {s} sn","{a} tarafÄ±ndan","CheatRunner'a hoÅ geldiniz.","HenÃ¼z hile dosyasÄ± yok. Kaynaklar'dan topluluk hilelerini indirin ya da bir oyunu aÃ§Ä±p dosya bÄ±rakÄ±n.","Ä°ptal","Devam","Yeniden baÄlanÄ±lÄ±yor...","KopyalandÄ±","KopyalandÄ±.","Bilinmiyor","Ä°letiÅim kutusu","KapalÄ±","AyrÄ±calÄ±klar","Tamam","Eksik","BaÅarÄ±sÄ±z.","BaÅlatÄ±lÄ±yor","Tekrar dene","Oyun","Hile ara","maj0r'Ä± X'te takip edin","maj0r'Ä± Bluesky'da takip edin","YapÄ±mcÄ±: maj0r - ð§ð·","Projeye bir yÄ±ldÄ±z bÄ±rakÄ±n! â­","KÄ±zÄ±l","Otomatik yÃ¼kleme","SÄ±fÄ±rla","Bu oyun iÃ§in yama yok.","XML yamalarÄ±nÄ±zÄ± /data/cheatrunner/patches/xml klasÃ¶rÃ¼ne koyun","GÃ¼ncelleme mevcut","{v} sÃ¼rÃ¼mÃ¼ mevcut.","Hileleri Ã§evrimiÃ§i ara","Hileli tÃ¼m oyunlarÄ± ara ve oyununuzun kapsanÄ±p kapsanmadÄ±ÄÄ±na bak.","Siteyi aÃ§","Topluluk yama depolarÄ±","OyunlarÄ±nÄ±z iÃ§in hazÄ±r yamalar. HiÃ§bir Åey indirilmez â depo yeni sekmede aÃ§Ä±lÄ±r.","PS-Game-Patch'i aÃ§","GÃ¶rsel efektler","AzaltÄ±lmÄ±Å mod bulanÄ±klÄ±ÄÄ±, gÃ¶lgeleri ve animasyonlarÄ± kapatÄ±r. PS5 tarayÄ±cÄ±sÄ±nda varsayÄ±landÄ±r.","Tam","AzaltÄ±lmÄ±Å","Daha fazla gÃ¶ster ({n})","Ãnerilen","Resmi PS-Game-Patch sÃ¼rÃ¼mÃ¼. Yamalar /data/cheatrunner/patches klasÃ¶rÃ¼ne kaydedilir.","YamalarÄ± indir","Yama dosyalarÄ± kaydediliyor","BazÄ± yamalar yalnÄ±zca oyun baÅlarken uygulanÄ±rsa Ã§alÄ±ÅÄ±r: otomatik yÃ¼klemeyi kaydedin ve oyunu yeniden baÅlatÄ±n.","Sorun giderme","Bir sorunu bildirin veya tÃ¼m ayarlarÄ± varsayÄ±lana dÃ¶ndÃ¼rÃ¼n.","TanÄ±lama bilgilerini kopyalar ve GitHub'daki sorun sayfasÄ±nÄ± aÃ§ar.","Hileler, yamalar ve otomatik yÃ¼kleme profillerine dokunulmaz.","Her Åey yolunda gÃ¶rÃ¼nÃ¼yor.","Dikkat gerektiren bir Åey var - aÅaÄÄ±daki ayrÄ±ntÄ±lara bakÄ±n.","BilgisayarÄ±nÄ±zdan bir .mc4, .shn veya .json dosyasÄ±.","Dosyalar veya Kaynaklar bÃ¶lÃ¼mÃ¼nden ekleyin","GÃ¶rÃ¼nÃ¼m","GÃ¶rÃ¼nÃ¼m, hile motoru ve kontrolcÃ¼ seÃ§enekleri. DeÄiÅiklikler anÄ±nda kaydedilir.","Hile dosyasÄ±","Bu oyun iÃ§in hile dosyalarÄ±","CheatRunner bu konsolda yÃ¼klÃ¼ oyunlarÄ± listeler. Bir oyun yÃ¼kleyin ve listeyi yenileyin.","AÃ§Ä±lana kadar hileler ve yamalar uygulanamaz.","Denetleniyor...","BaÅka dosya seÃ§","YÃ¼klenemedi","Hile dosyasÄ± yÃ¼klenemedi","Yamalar yÃ¼klenemedi","Ayarlar yÃ¼klenemedi","OyunlarÄ±nÄ±z yÃ¼klenemedi","Hilelerin gÃ¼ncel durumu okunamadÄ±.","Kaynaklar bÃ¶lÃ¼mÃ¼nden topluluk hilelerini indirin veya bilgisayarÄ±nÄ±zdan bir dosya ekleyin.","Kaynaklar bÃ¶lÃ¼mÃ¼nden indirin veya aÅaÄÄ±ya bir dosya bÄ±rakÄ±n.","OyunlarÄ±nÄ±z iÃ§in hazÄ±r hile ve yamalarÄ± indirin. Gezinirken indirmeler devam eder.","ResmÃ® yama paketini Kaynaklar bÃ¶lÃ¼mÃ¼nden indirin veya XML yamalarÄ±nÄ±zÄ± /data/cheatrunner/patches/xml klasÃ¶rÃ¼ne koyun.","Ä°ndiriliyor","Ä°ndiriliyor {p}%","Ä°ndirmeler","Motor","En son satÄ±rlar izleniyor","Hile al","Durum","KÃ¼tÃ¼phane","Hile motorunun ve bellek yamalamanÄ±n canlÄ± durumu ve CheatRunnerâÄ±n yaptÄ±klarÄ±.","Haberler, yardÄ±m ve CheatRunnerâÄ±n arkasÄ±ndaki insanlar.","Bu oyun iÃ§in henÃ¼z hile dosyasÄ± yok","\"{q}\" ile eÅleÅen hile yok.","HenÃ¼z dosya yok","HenÃ¼z oyun bulunamadÄ±","Yerel dosya bulunamadÄ±","Bu oyun iÃ§in yama yok","Åu an oynanÄ±yor","AyarlarÄ± aÃ§","Okurken duraklatÄ±ldÄ±","Tercihler","AzaltÄ±lmÄ±Å mod tÃ¼m animasyonlarÄ± kaldÄ±rÄ±r. PS5 tarayÄ±cÄ±sÄ±nda varsayÄ±landÄ±r.","/data/cheatrunner/cheats iÃ§inde saklanÄ±r. \"KullanÄ±mda\" iÅaretli dosyayÄ± trainer okur.","Bir Åey Ã§alÄ±ÅmadÄ±ÄÄ±nda rapor gÃ¶nderin.","Hileleri kullanmak iÃ§in baÅlatÄ±n","Hilelerini kullanmak iÃ§in bu oyunu baÅlatÄ±n.","Teknik ayrÄ±ntÄ±lar","Hile motoru kapalÄ±.","Mevcut deÄerler okunamadÄ±; yanlÄ±Å deÄer gÃ¶stermemek iÃ§in hiÃ§bir Åey gÃ¶sterilmiyor.","Hile ve yamalarÄ± uygulamak iÃ§in temel ayarlar.","Dosya var ama okunamadÄ±. Bozuk olabilir veya hÃ¢lÃ¢ kopyalanÄ±yor olabilir.","Dosya yÃ¼klendi ama boÅ. Bu oyun iÃ§in baÅka bir dosya deneyin.","Oyun listesi Åu anda kullanÄ±lamÄ±yor. Konsolun aÃ§Ä±k olduÄundan emin olup tekrar deneyin.","Bu oyunun yama dosyalarÄ± okunamadÄ±. Birazdan tekrar deneyin.","Anahtarlar gÃ¼ncel olmayabilir.","Bu panelin temasÄ±, efektleri ve dili.","Bu hile dosyasÄ±nda hile yok","AÅaÄÄ±dan tekrar deneyin","Ayarlar bÃ¶lÃ¼mÃ¼nden aÃ§Ä±n","Tehlikeli bÃ¶lge","En yeniye git","KullanÄ±mdaki dosyayÄ± sil","Hile kaynaÄÄ±","OyunlarÄ± filtrele","OyunlarÄ±nÄ±za bakÄ±n","EmeÄi geÃ§enler","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"uk-UA":["Ð§Ð¸Ñ Ð¼Ð¸Ð½ÑÐ»Ð¾Ð³Ð¾ ÑÐ°Ð·Ñ Ð¿ÑÐ¸Ð·Ð²ÑÐ² Ð´Ð¾ Ð·Ð±Ð¾Ñ Ð³ÑÐ¸.","ÐÑÐ¾ Ð¿ÑÐ¾Ð³ÑÐ°Ð¼Ñ","ÐÑÑ","ÐÑÑ","ÐÐ¾Ð´Ð°ÑÐ¸ ÑÐ°Ð¹Ð»","ÐÐ¾Ð´Ð°ÑÐ¸ ÑÐ¸ÑÐ¸","ÐÐ¾Ð´Ð°ÑÐ¸ Ð´Ð¾ Ð²Ð¸Ð±ÑÐ°Ð½Ð¾Ð³Ð¾","ÐÐ¾Ð´Ð°Ñ Ð¿ÑÐ½ÐºÑ â CheatRunner Ð´Ð¾ Ð¼ÐµÐ½Ñ Ð¿Ð°ÑÐ°Ð¼ÐµÑÑÑÐ² ÐºÐ¾Ð¶Ð½Ð¾Ñ Ð³ÑÐ¸ ÑÐ° ÐºÐ¾Ð¼Ð±ÑÐ½Ð°ÑÑÑ ÐºÐ½Ð¾Ð¿Ð¾Ðº, ÑÐ¾ Ð²ÑÐ´ÐºÑÐ¸Ð²Ð°Ñ ÑÑÐµÐ¹Ð½ÐµÑ Ð·Ð°Ð¿ÑÑÐµÐ½Ð¾Ñ Ð³ÑÐ¸.","ÐÐ¾Ð´Ð°ÑÐºÐ¾Ð²Ð¾","Ð£ÑÑ","Ð£ÑÑ Ð²Ð½ÐµÑÐºÐ¸ Ð¿ÑÐ´ÑÑÑ Ð½Ð° Ð¿ÑÐ¸Ð´Ð±Ð°Ð½Ð½Ñ PS5 Ð· Ð½Ð¸Ð·ÑÐºÐ¾Ñ Ð¿ÑÐ¾ÑÐ¸Ð²ÐºÐ¾Ñ (1.xxâ4.xx) Ð´Ð»Ñ Ð´Ð¾ÑÐ»ÑÐ´Ð¶ÐµÐ½Ñ, ÑÐ¾Ð·ÑÐ¾Ð±ÐºÐ¸ ÑÐ° Ð¼Ð°Ð¹Ð±ÑÑÐ½ÑÑ Ð¾Ð½Ð¾Ð²Ð»ÐµÐ½Ñ.","Ð£ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ Ð½Ðµ Ð²Ð´Ð°Ð»Ð¸ÑÑ","Ð£ÑÑ ÑÐ³ÑÐ¸","Ð¢Ð°ÐºÐ¾Ð¶ Ð¾Ð½Ð¾Ð²Ð»ÑÐ²Ð°ÑÐ¸ Ð·Ð¼ÑÐ½ÐµÐ½Ñ ÑÐ°Ð¹Ð»Ð¸","ÐÐ°ÑÑÐ¾ÑÐ¾Ð²Ð°Ð½Ð¾","ÐÐ°ÑÑÐ¾ÑÑÐ²Ð°ÑÐ¸","ÐÐ°ÑÑÐ¾ÑÑÐ½ÐºÐ¸","ÐÐ²ÑÐ¾Ð²Ð¸Ð±ÑÑ","ÐÐ°Ð·Ð°Ð´","ÐÐµÑÐµÐ³Ð»ÑÐ´ Ð±ÑÐ±Ð»ÑÐ¾ÑÐµÐºÐ¸","ÐÐ½Ð¾Ð¿ÐºÐ¸","Buy Me a Coffee","Ð§Ð¸Ñ","Ð ÑÑÑÐ¹ ÑÐ¸ÑÑÐ²","CheatRunner â ÑÐ¸Ñ-ÑÑÐµÐ¹Ð½ÐµÑ Ð´Ð»Ñ Ð²Ð°ÑÐ¾Ñ PS5!","Ð§Ð¸ÑÐ¸","Ð§Ð¸ÑÐ¸ ÑÑÑÐ¸Ð¼ÑÑÑÑÑÑ ÑÐµÐ¹ ÑÐ°Ñ Ð¿ÑÑÐ»Ñ Ð·Ð°Ð¿ÑÑÐºÑ Ð³ÑÐ¸.","Ð¡Ð¿Ð¾ÑÐ°ÑÐºÑ Ð· ÑÐ¸ÑÐ°Ð¼Ð¸","ÐÐ¸Ð±ÐµÑÑÑÑ Ð¼ÐµÑÐµÐ¶Ñ","Circle","Ð¡ÐºÐ¸Ð½ÑÑÐ¸ ÑÑÐ»ÑÑÑÐ¸","ÐÐ½ÑÑÐ¸ Ð¿Ð¾Ð·Ð½Ð°ÑÐºÑ","ÐÑÐ¸ÑÑÐ¸ÑÐ¸ Ð¿Ð¾ÑÑÐº","ÐÐ°ÐºÑÐ¸ÑÐ¸","ÐÐ³Ð¾ÑÐ½ÑÑÐ¸","ÐÐ³Ð¾ÑÐ½ÑÑÐ¸ Ð¼ÐµÐ½Ñ","ÐÐ¾Ð»ÑÐ¾ÑÐ¾Ð²Ð° ÑÑÐµÐ¼Ð° Ð¿Ð°Ð½ÐµÐ»Ñ.","Ð¡Ð¿ÑÐ»ÑÐ½Ð¾ÑÐ°","Ð ÐµÐ¿Ð¾Ð·Ð¸ÑÐ¾ÑÑÑ ÑÐ¸ÑÑÐ² ÑÐ¿ÑÐ»ÑÐ½Ð¾ÑÐ¸","ÐÐ¾Ð¼Ð±ÑÐ½Ð°ÑÑÑ Ð½Ð° Ð³ÐµÐ¹Ð¼Ð¿Ð°Ð´Ñ ÑÐ° Ð¼ÐµÐ½Ñ Ð³ÑÐ¸","ÐÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸","ÐÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸ URL","ÐÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸ Ð²ÑÐµ","ÐÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸ ÑÐµÐºÑ ÑÐ¸ÑÑÐ²","ÐÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸ Ð¿Ð¾ÑÐ¸Ð»Ð°Ð½Ð½Ñ","ÐÐ¸ÑÐ²Ð»ÐµÐ½Ð½Ñ Ð·Ð±Ð¾ÑÐ²","ÐÐ¸Ð½ÑÐ»Ð¾Ð³Ð¾ ÑÐ°Ð·Ñ ÑÐ¿ÑÐ¸ÑÐ¸Ð½Ð¸Ð² Ð·Ð±ÑÐ¹ Ð³ÑÐ¸","Cross","ÐÑÐ¸Ð¿ÑÐ¾","ÐÑÐ´ÑÑÐ¸Ð¼ÐºÐ° ÐºÑÐ¸Ð¿ÑÐ¾Ð²Ð°Ð»ÑÑÐ¾Ñ","Ð¢ÐµÐ¼Ð½Ð°","ÐÐ¸Ð´Ð°Ð»Ð¸ÑÐ¸","ÐÐ¸Ð¼ÐºÐ½ÑÑÐ¸ Ð²ÑÑ","ÐÐ½Ð¸Ð·","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ Ð²ÑÑ Ð´Ð¶ÐµÑÐµÐ»Ð°","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÑÐµ ÑÐ¸ÑÐ¸ ÑÐ¿ÑÐ»ÑÐ½Ð¾ÑÐ¸","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÑÐ¹ÑÐµ Ð³Ð¾ÑÐ¾Ð²Ñ ÑÐ°Ð¹Ð»Ð¸ ÑÐ¸ÑÑÐ² Ð´Ð»Ñ Ð²Ð°ÑÐ¸Ñ ÑÐ³Ð¾Ñ. ÐÐ¾Ð½Ð¸ Ð·Ð±ÐµÑÑÐ³Ð°ÑÑÑÑÑ Ð² /data/cheatrunner/cheats, Ð° Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ ÑÑÐ¸Ð²Ð°Ñ Ñ ÑÐ¾Ð½Ñ.","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ Ð²Ð¸Ð±ÑÐ°Ð½Ðµ","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð¾","ÐÐµÑÐµÑÑÐ³Ð½ÑÑÑ ÑÑÐ´Ð¸ ÑÐ°Ð¹Ð» .mc4, .shn Ð°Ð±Ð¾ .json Ð°Ð±Ð¾ Ð½Ð°ÑÐ¸ÑÐ½ÑÑÑ Ð´Ð»Ñ Ð²Ð¸Ð±Ð¾ÑÑ","Ð£Ð²ÑÐ¼ÐºÐ½ÑÑÐ¸ ÐºÐ¾Ð¼Ð±ÑÐ½Ð°ÑÑÑ Ð½Ð° Ð³ÐµÐ¹Ð¼Ð¿Ð°Ð´Ñ","Ð£Ð²ÑÐ¼ÐºÐ½ÑÑÐ¸ ÐºÐ¾Ð¼Ð±ÑÐ½Ð°ÑÑÑ","Ð ÑÑÑÐ¹ ÑÐ²ÑÐ¼Ðº.","Enter â Ð²ÑÐ´ÐºÑÐ¸ÑÐ¸","ÐÐ¸ÑÐµ Ð¿Ð¾Ð¼Ð¸Ð»ÐºÐ¸","Ð¢Ð¾ÑÐ½Ð°","ÐÐ¾Ð¼Ð¸Ð»ÐºÐ¸","ÐÐ¸Ð±ÑÐ°Ð½Ðµ","Ð¤Ð°Ð¹Ð»Ð¸","Ð¤ÑÐ»ÑÑÑ","Ð¤ÑÐ»ÑÑÑ Ð¶ÑÑÐ½Ð°Ð»Ñ","ÐÐ¾Ð·Ð½Ð°ÑÐ°ÑÐ¸ ÑÐ¸Ñ, ÑÐºÑÐ¾ Ð³ÑÐ° Ð·Ð°ÐºÑÐ¸Ð»Ð°ÑÑ Ð¾Ð´ÑÐ°Ð·Ñ Ð¿ÑÑÐ»Ñ Ð¹Ð¾Ð³Ð¾ Ð²Ð¼Ð¸ÐºÐ°Ð½Ð½Ñ.","ÐÐ»Ñ ÐºÐ¾ÑÐ¸ÑÑÑÐ²Ð°ÑÑÐ². ÐÑÐ´ ÐºÐ¾ÑÐ¸ÑÑÑÐ²Ð°ÑÑÐ².","ÐÐ°ÑÑÐ¾ÑÑÐ²Ð°ÑÐ¸ Ð¿ÑÐ¸Ð¼ÑÑÐ¾Ð²Ð¾","ÐÐ³ÑÐ¸","ÐÐ³ÑÐ¸ Ð· ÑÐ¸ÑÐ°Ð¼Ð¸","ÐÐ³ÑÐ¸ Ð· Ð¿Ð°ÑÑÐ°Ð¼Ð¸","ÐÐ°ÑÐ·Ð° Ð¼ÑÐ¶ ÑÐ¸ÑÐ°Ð¼Ð¸ (Ð¼Ñ)","ÐÐ°Ð³Ð°Ð»ÑÐ½Ñ","Ð£ÑÑÐ¸Ð¼ÑÑÑÑÑÑ ÑÐ°Ð·Ð¾Ð¼ Ð´Ð»Ñ Ð·Ð°Ð¿ÑÑÐºÑ ÐºÐ¾Ð¼Ð±ÑÐ½Ð°ÑÑÑ.","Ð§Ð°Ñ ÑÑÑÐ¸Ð¼Ð°Ð½Ð½Ñ (Ð¼Ñ)","Ð£ÑÑÐ¸Ð¼ÑÐ¹ÑÐµ Ð´Ð²Ñ ÐºÐ½Ð¾Ð¿ÐºÐ¸ ÑÐ°Ð·Ð¾Ð¼, ÑÐ¾Ð± Ð²ÑÐ´ÐºÑÐ¸ÑÐ¸ ÑÑÐµÐ¹Ð½ÐµÑ Ð·Ð°Ð¿ÑÑÐµÐ½Ð¾Ñ Ð³ÑÐ¸.","ÐÐ»Ð¸ÑÐºÐ° Ð½Ð° Ð³Ð¾Ð»Ð¾Ð²Ð½Ð¾Ð¼Ñ ÐµÐºÑÐ°Ð½Ñ","ÐÑÐ´ÐºÐ»ÑÑÐ°ÑÑÑÑÑ Ð´Ð¾ ÑÐ½ÑÐµÑÑÐµÐ¹ÑÑ ÑÐ¸ÑÑÐµÐ¼Ð¸. ÐÐ° Ð·Ð°Ð¼Ð¾Ð²ÑÑÐ²Ð°Ð½Ð½ÑÐ¼ Ð²Ð¸Ð¼ÐºÐ½ÐµÐ½Ð¾ â Ð½Ð° Ð´ÐµÑÐºÐ¸Ñ Ð¿ÑÐ¾ÑÐ¸Ð²ÐºÐ°Ñ Ð¼Ð¾Ð¶Ðµ ÑÐ¿ÑÐ¸ÑÐ¸Ð½Ð¸ÑÐ¸ Ð·Ð±ÑÐ¹ Ð³Ð¾Ð»Ð¾Ð²Ð½Ð¾Ð³Ð¾ ÐµÐºÑÐ°Ð½Ð°.","Ð¯Ðº Ð´Ð¾Ð²Ð³Ð¾ ÑÑÑÐ¸Ð¼ÑÐ²Ð°ÑÐ¸ ÐºÐ¾Ð¼Ð±ÑÐ½Ð°ÑÑÑ Ð´Ð¾ ÑÐ¿ÑÐ°ÑÑÐ²Ð°Ð½Ð½Ñ.","ÐÐ°ÑÐºÑÐ»ÑÐºÐ¸ Ð´Ð¾ÐºÐ»Ð°Ð´Ð½Ð¾ Ð·Ð°Ð¿Ð¸ÑÑÑÑÑÑÑ Ð¶ÑÑÐ½Ð°Ð».","Ð¯ÐºÑÐ¾ CheatRunner ÑÑÐ°Ð² Ð²Ð°Ð¼ Ñ Ð¿ÑÐ¸Ð³Ð¾Ð´Ñ, Ð¿ÑÐ´ÑÑÐ¸Ð¼Ð°Ð¹ÑÐµ ÑÐ¾Ð·Ð²Ð¸ÑÐ¾Ðº Ð¿ÑÐ¾ÑÐºÑÑ! ð«¶","ÐÐ¸ÐºÐ¾ÑÐ¸ÑÑÐ¾Ð²ÑÑÑÑÑÑ","ÐÑÑÐ°Ð½Ð¾Ð²Ð»ÑÑ Ð¿Ð»Ð¸ÑÐºÑ CheatRunner Ð½Ð° Ð³Ð¾Ð»Ð¾Ð²Ð½Ð¸Ð¹ ÐµÐºÑÐ°Ð½ PS5 Ð¿ÑÐ´ ÑÐ°Ñ Ð·Ð°Ð¿ÑÑÐºÑ.","ÐÑÐ¸ÑÐ´Ð½Ð°ÑÐ¸ÑÑ Ð´Ð¾ Discord","Ko-Fi, ÐºÐ°Ð²Ð°, ÐºÑÐ¸Ð¿ÑÐ¾, PIX","ÐÐ¾Ð²Ð°","ÐÐ¾Ð²Ð° ÑÑÑÑ Ð¿Ð°Ð½ÐµÐ»Ñ.","ÐÑÑÐ°Ð½Ð½Ñ Ð³ÑÐ°","ÐÐ»ÑÐ²Ð¾","Ð¡Ð²ÑÑÐ»Ð°","Ð¡ÑÐ°Ð½ ÑÑÑÑÑ ÑÐ¸ÑÑÐ² Ñ Ð·Ð°Ð¿Ð¸ÑÑ Ð² Ð¿Ð°Ð¼'ÑÑÑ.","ÐÐ¾ÐºÐ°Ð»ÑÐ½Ñ ÑÐ°Ð¹Ð»Ð¸ ÑÐ¸ÑÑÐ²","Ð ÑÐ²ÐµÐ½Ñ Ð¶ÑÑÐ½Ð°Ð»Ñ","Ð¤ÑÐ»ÑÑÑ ÑÑÐ²Ð½Ñ Ð¶ÑÑÐ½Ð°Ð»Ñ","ÐÑÑÐ½Ð°Ð»Ð¸","ÐÑÐ½Ð¾Ð²Ð½Ðµ","ÐÐ¾Ð»Ð¾Ð²Ð½Ð¸Ð¹ Ð²Ð¸Ð¼Ð¸ÐºÐ°Ñ Ð·Ð°ÑÑÐ¾ÑÑÐ²Ð°Ð½Ð½Ñ ÑÐ¸ÑÑÐ² Ñ Ð¿Ð°ÑÑÑÐ².","ÐÐ¸Ð¿ÑÐ°Ð²Ð»ÐµÐ½Ð½Ñ Ð¼Ð°Ð¹ÑÑÐµÑ-ÐºÐ¾Ð´Ñ","ÐÐ¾Ð¶Ðµ Ð½Ðµ Ð²ÑÐ´Ð¿Ð¾Ð²ÑÐ´Ð°ÑÐ¸ ÑÑÐ¹ Ð²ÐµÑÑÑÑ Ð³ÑÐ¸","ÐÐ°ÑÑÐ¸Ð½Ð³ Ð¿Ð°Ð¼'ÑÑÑ","ÐÑÐ²Ð½ÑÑ","ÐÑÐ½ÑÐ¼Ð°Ð»ÑÐ½Ð° Ð¿Ð°ÑÐ·Ð° Ð¼ÑÐ¶ Ð´Ð²Ð¾Ð¼Ð° Ð¿ÐµÑÐµÐ¼Ð¸ÐºÐ°Ð½Ð½ÑÐ¼Ð¸ ÑÐ¸ÑÑÐ².","ÐÐ°Ð·Ð²Ð° Ð-Ð¯","ÐÐ°Ð·Ð²Ð° Ð¯-Ð","ÐÐ»Ñ ÑÑÑÑ Ð³ÑÐ¸ ÑÐµ Ð½ÐµÐ¼Ð°Ñ ÑÐ°Ð¹Ð»Ñ ÑÐ¸ÑÑÐ².","ÐÐ¸Ð±ÑÐ°Ð½Ð¾Ð³Ð¾ Ð¿Ð¾ÐºÐ¸ Ð½ÐµÐ¼Ð°Ñ. ÐÐ°ÑÐ¸ÑÐ½ÑÑÑ Ð·ÑÑÐ¾ÑÐºÑ Ð±ÑÐ»Ñ Ð³ÑÐ¸, ÑÐ¾Ð± Ð´Ð¾Ð´Ð°ÑÐ¸.","ÐÐµ Ð·Ð°Ð¿ÑÑÐµÐ½Ð°","Ð£Ð²ÑÐ¼Ðº.","ÐÑÐ´ÐºÑÐ¸ÑÐ¸ Ð½Ð° ÑÐµÐ»ÐµÑÐ¾Ð½Ñ","ÐÑÐ´ÐºÑÐ¸ÑÐ¸ Ð½Ð° ÑÐµÐ»ÐµÑÐ¾Ð½Ñ (QR-ÐºÐ¾Ð´)","ÐÑÐ´ÐºÑÐ¸Ð¹ÑÐµ Ð¿Ð°Ð½ÐµÐ»Ñ Ð½Ð° ÑÐµÐ»ÐµÑÐ¾Ð½Ñ","ÐÑÐ´ÐºÑÐ¸ÑÐ¸ ÑÑÐµÐ¹Ð½ÐµÑ","ÐÐ°ÑÐ°Ð¼ÐµÑÑÐ¸","ÐÐ½ÑÐ° Ð²ÐµÑÑÑÑ","Ð¡ÑÐ¾ÑÑÐ½ÐºÐ°","ÐÐ°ÑÑÑ","ÐÑÐ°ÑÐ¸","QR-ÐºÐ¾Ð´","QR-ÐºÐ¾Ð´ Ð°Ð´ÑÐµÑÐ¸ Ð¿Ð°Ð½ÐµÐ»Ñ","ÐÐ¾Ð²ÑÐ¾ÑÐ½Ð¾ Ð·Ð°ÑÑÐ¾ÑÐ¾Ð²ÑÐ²Ð°ÑÐ¸ Ð²Ð²ÑÐ¼ÐºÐ½ÐµÐ½Ñ ÑÐ¸ÑÐ¸ Ð¿ÑÐ¸ ÐºÐ¾Ð¶Ð½Ð¾Ð¼Ñ Ð·Ð°Ð¿ÑÑÐºÑ ÑÑÑÑ Ð³ÑÐ¸","ÐÐ¾ÑÐ¾Ð²Ð¾","ÐÐµÑÐ¾Ð´Ð°Ð²Ð½Ñ","ÐÐ½Ð¾Ð²Ð¸ÑÐ¸","ÐÐ½Ð¾Ð²Ð¸ÑÐ¸ ÑÐ¿Ð¸ÑÐ¾Ðº ÑÐ³Ð¾Ñ","ÐÐµÑÐµÐ·Ð°Ð¿ÑÑÑÐ¸ÑÐ¸","ÐÐ¾Ð²ÑÐ´Ð¾Ð¼Ð¸ÑÐ¸ Ð¿ÑÐ¾ Ð¿ÑÐ¾Ð±Ð»ÐµÐ¼Ñ","ÐÐµÑÐµÑÐºÐ°Ð½ÑÐ²Ð°ÑÐ¸ Ð²ÑÑÐ°Ð½Ð¾Ð²Ð»ÐµÐ½Ñ ÑÐ³ÑÐ¸","Ð¡ÐºÐ¸Ð½ÑÑÐ¸ Ð½Ð°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½Ñ","ÐÐ²ÑÐ¾Ð¼Ð°ÑÐ¸ÑÐ½Ð¾ ÑÐ¾Ð·Ð²'ÑÐ·ÑÐ²Ð°ÑÐ¸ ÑÐ¸ÑÐ¸, ÑÐ¾ Ð·Ð°Ð»ÐµÐ¶Ð°ÑÑ Ð²ÑÐ´ Ð¼Ð°Ð¹ÑÑÐµÑ-ÐºÐ¾Ð´Ñ.","ÐÑÐ´Ð½Ð¾Ð²Ð¸ÑÐ¸","ÐÐ¿ÑÐ°Ð²Ð¾","ÐÐ°Ð¿ÑÑÐµÐ½Ð°","ÐÐ°Ð¿ÑÑÐµÐ½Ð° Ð·Ð°ÑÐ°Ð·","ÐÐ±ÐµÑÐµÐ³ÑÐ¸ Ð°Ð²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ","Ð¡ÐºÐ°Ð½ÑÐ¹ÑÐµ ÐºÐ°Ð¼ÐµÑÐ¾Ñ ÑÐµÐ»ÐµÑÐ¾Ð½Ð°. Ð¢ÐµÐ»ÐµÑÐ¾Ð½ Ð¼Ð°Ñ Ð±ÑÑÐ¸ Ð² Ð¾Ð´Ð½ÑÐ¹ Ð¼ÐµÑÐµÐ¶Ñ Wi-Fi Ð· PS5.","ÐÐ¾ÑÑÐº","ÐÐ¾ÑÑÐº Ð·Ð° Ð½Ð°Ð·Ð²Ð¾Ñ Ð°Ð±Ð¾ ID","ÐÐ¾ÑÑÐº ÑÐ³Ð¾Ñ","ÐÐ¾ÑÑÐº ÑÐ³Ð¾Ñ, ÑÑÐ¾ÑÑÐ½Ð¾Ðº Ñ Ð´ÑÐ¹","ÐÐ¸Ð±ÐµÑÑÑÑ Ð°Ð´ÑÐµÑÑ, ÑÐ¾Ð± ÑÐºÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸ ÑÑ Ð¿Ð¾Ð²Ð½ÑÑÑÑ.","ÐÐ°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½Ñ","ÐÐ¾ÐºÐ°Ð·Ð°ÑÐ¸ QR-ÐºÐ¾Ð´","ÐÑÐ¿Ð¸Ð½Ð¸ÑÐ¸ payload","Ð¡Ð¾ÑÑÑÐ²Ð°Ð½Ð½Ñ ÑÐ³Ð¾Ñ","ÐÐ¶ÐµÑÐµÐ»Ð¾","ÐÐ¶ÐµÑÐµÐ»Ð°","Ð¨Ð²Ð¸Ð´ÐºÑÑÑÑ","Square","ÐÐ°Ð¿ÑÑÑÑÑÑ Ð³ÑÑ","ÐÐ°Ð¿ÑÑÑÑÑÑ ÑÑ Ð³ÑÑ, ÑÐ¾Ð± Ð·Ð°ÑÑÐ¾ÑÐ¾Ð²ÑÐ²Ð°ÑÐ¸ Ð¿Ð°ÑÑÑ.","Ð¡ÑÐ°Ð½","Ð¡ÑÐ°Ð½ Ñ Ð¶ÑÑÐ½Ð°Ð»Ð¸","ÐÑÐ´ÑÑÐ¸Ð¼Ð°ÑÐ¸ CheatRunner","ÐÑÐ´ÑÑÐ¸Ð¼ÐºÐ° Ð· ÐÑÐ°Ð·Ð¸Ð»ÑÑ","ÐÑÐ´ÑÑÐ¸Ð¼Ð°ÑÐ¸ Ð¿ÑÐ¾ÑÐºÑ","Ð¡Ð¿Ð¾Ð½ÑÐ¾ÑÐ¸","Ð¡Ð¸ÑÑÐµÐ¼Ð°","ÐÐ¾Ð»Ð¾Ð²Ð½Ðµ: ÑÐµÐ¼Ð° ÑÐ° Ð²Ð¸Ð¼Ð¸ÐºÐ°Ñ ÑÑÑÑÑ ÑÐ¸ÑÑÐ².","Ð¢ÐµÐ¼Ð°","Ð¢ÐµÐ¼Ð° ÑÐ° ÑÑÑÑÐ¹ ÑÐ¸ÑÑÐ²","ÐÐ°Ð»Ð¸ÑÐ¸Ð»Ð¾ÑÑ","Ð§Ð°Ñ Ð³ÑÐ¸","ID Ð³ÑÐ¸","ÐÐµÑÐµÐ¼Ð¸ÐºÐ°ÑÑ","TouchPad","Ð¢ÑÐµÐ¹Ð½ÐµÑ","Triangle","ÐÐ³Ð¾ÑÑ","ÐÐºÑÑÐ°Ð»ÑÐ½Ñ","ÐÐ¸ÐºÐ¾ÑÐ¸ÑÑÐ°ÑÐ¸","ÐÐµÑÑÑÑ","ÐÐ°ÑÑÐ¸Ð¼ÐºÐ° Ð¿ÑÑÐ»Ñ Ð·Ð°Ð¿ÑÑÐºÑ Ð³ÑÐ¸ (Ð¼Ñ)","ÐÐ¾Ð¿ÐµÑÐµÐ´Ð¶ÐµÐ½Ð½Ñ ÑÐ° Ð¿Ð¾Ð¼Ð¸Ð»ÐºÐ¸","Ð©Ð¾ ÑÐ¾Ð±Ð¸Ð² CheatRunner â ÐºÐ¾ÑÐ¸ÑÐ½Ð¾, ÐºÐ¾Ð»Ð¸ ÑÐ¾ÑÑ Ð½Ðµ Ð¿ÑÐ°ÑÑÑ.","Ð ÑÐ¸ÑÐ°Ð¼Ð¸","ââ Ð½Ð°Ð²ÑÐ³Ð°ÑÑÑ","Ð£ÑÑ Ð½Ð°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½Ñ Ð¿Ð¾Ð²ÐµÑÐ½ÑÑÑÑÑ Ð´Ð¾ ÑÐ¸Ð¿Ð¾Ð²Ð¸Ñ. ÐÐµÐ±-Ð¿Ð¾ÑÑ Ð±ÑÐ´Ðµ Ð·Ð±ÐµÑÐµÐ¶ÐµÐ½Ð¾.","ÐÐ°ÑÑÐ¾ÑÐ¾Ð²ÑÑÑÑÑÑ ÑÐ½ÑÐ¸Ð¹ ÑÐ¸Ñ. ÐÐ¾Ð²ÑÐ¾ÑÑÑÑ ÑÑÐ¾ÑÐ¸ Ð·Ð³Ð¾Ð´Ð¾Ð¼.","ÐÑÐ´Ñ-ÑÐºÐ° Ð²ÐµÑÑÑÑ","ÐÐ°ÑÑÐ¾ÑÑÐ²Ð°ÑÐ¸ Ð²ÑÐµ Ð¾Ð´Ð½Ð¾","ÐÐ²ÑÐ¾Ð²Ð¸Ð±ÑÑ Ð²ÑÐ´Ð½Ð¾Ð²Ð»ÐµÐ½Ð¾.","ÐÐ²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ Ð¾ÑÐ¸ÑÐµÐ½Ð¾.","ÐÐ²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ Ð·Ð±ÐµÑÐµÐ¶ÐµÐ½Ð¾ ({n}). ÐÐ¾Ð½Ð¸ Ð²Ð²ÑÐ¼ÐºÐ½ÑÑÑÑÑ Ð¿ÑÐ´ ÑÐ°Ñ Ð·Ð°Ð¿ÑÑÐºÑ Ð³ÑÐ¸.","ÐÐ±ÐµÑÐµÐ¶ÐµÐ½Ð¾ Ð¿Ð¾ÑÐ¾Ð¶Ð½Ñ Ð°Ð²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ.","Ð§Ð¸Ñ â{n}","Ð¤Ð°Ð¹Ð» ÑÐ¸ÑÑÐ² Ð´Ð¾Ð´Ð°Ð½Ð¾.","Ð¤Ð°Ð¹Ð» ÑÐ¸ÑÑÐ² Ð²Ð¸Ð´Ð°Ð»ÐµÐ½Ð¾.","Ð¤Ð°Ð¹Ð» ÑÐ¸ÑÑÐ² Ð²Ð¸Ð±ÑÐ°Ð½Ð¾.","CheatRunner Ð¿ÐµÑÐµÑÑÐ°Ð² Ð²ÑÐ´Ð¿Ð¾Ð²ÑÐ´Ð°ÑÐ¸ Ð¿ÑÐ´ ÑÐ°Ñ Ð·Ð°Ð¿ÑÑÐºÑ.","Ð¢ÐµÐºÑ ÑÐ¸ÑÑÐ² ÑÐºÐ¾Ð¿ÑÐ¹Ð¾Ð²Ð°Ð½Ð¾.","ÐÐµÑÐµÐ²ÑÑÐºÐ°: {name}","ÐÑÐ¸ÑÑÐ¸ÑÐ¸ Ð°Ð²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ ({n})","ÐÐ¾ÑÑÐ²Ð½ÑÐ½Ð½Ñ ÑÐ° Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ ÑÐ°Ð¹Ð»ÑÐ² Ð¿Ð¾ Ð¾Ð´Ð½Ð¾Ð¼Ñ","ÐÐ¾Ð½ÑÐ»ÑÐºÑÑÑ Ð· ÑÐ½ÑÐ¸Ð¼ ÑÐ¸ÑÐ¾Ð¼","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ ÑÐºÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸. ÐÐ¸Ð´ÑÐ»ÑÑÑ ÑÐµÐºÑÑ Ð²ÑÑÑÐ½Ñ.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð¾ÑÐ¸ÑÑÐ¸ÑÐ¸ Ð°Ð²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ ÑÐºÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸ Ð´ÑÐ°Ð³Ð½Ð¾ÑÑÐ¸ÐºÑ.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð²Ð¸Ð¼ÐºÐ½ÑÑÐ¸ ÑÐ¸ÑÐ¸.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ Ð¿Ð°ÑÑÑ.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ ÑÐ¿Ð¸ÑÐ¾Ðº ÑÐ³Ð¾Ñ.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð±ÐµÑÐµÐ³ÑÐ¸ Ð°Ð²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð±ÐµÑÐµÐ³ÑÐ¸. Ð¡Ð¿ÑÐ¾Ð±ÑÐ¹ÑÐµ ÑÐµ ÑÐ°Ð·.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð²Ð¸Ð±ÑÐ°ÑÐ¸ ÑÐ°Ð¹Ð».","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð¿Ð¾ÑÐ°ÑÐ¸ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð¿ÑÑÑÐ¸ÑÐ¸.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð¿ÐµÑÐµÐ¼ÐºÐ½ÑÑÐ¸ ÑÐ¸Ñ.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð²Ð¸Ð´Ð°Ð»Ð¸ÑÐ¸.","ÐÐ¸Ð´Ð°Ð»Ð¸ÑÐ¸ ÑÐ°Ð¹Ð»","ÐÐ¸Ð´Ð°Ð»Ð¸ÑÐ¸ {file}?","ÐÑÐ°Ð³Ð½Ð¾ÑÑÐ¸ÐºÑ ÑÐºÐ¾Ð¿ÑÐ¹Ð¾Ð²Ð°Ð½Ð¾. ÐÑÑÐ°Ð²ÑÐµ ÑÑ Ñ Ð·Ð²ÐµÑÐ½ÐµÐ½Ð½Ñ.","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ: {name}","Ð ÑÑÑÐ¹ Ð²Ð¸Ð¼Ðº.","Ð£ÑÐµ Ð²Ð¶Ðµ Ð°ÐºÑÑÐ°Ð»ÑÐ½Ðµ","Ð Ð¾Ð·Ð³Ð¾ÑÐ½ÑÑÐ¸ Ð¼ÐµÐ½Ñ","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ Ð²ÑÑÐ¾Ð³Ð¾ ÑÐµÐ¿Ð¾Ð·Ð¸ÑÐ¾ÑÑÑ Ð¾Ð´ÑÐ°Ð·Ñ","Ð¤Ð°Ð¹Ð» v{a} / Ð³ÑÐ° v{b}","ÐÐ¾ÑÐ¾Ð²Ð¾: {name}","ÐÐ°ÑÑÐ¾ÑÑÐ²Ð°ÑÐ¸ {name} Ð¿ÑÐ¸Ð¼ÑÑÐ¾Ð²Ð¾?","ÐÑÑ Ð·Ð°Ð¿ÑÑÐµÐ½Ð¾.","ÐÑÐ½ ÑÐ¾Ð·ÑÐ°ÑÐ¾Ð²Ð°Ð½Ð¸Ð¹ Ð½Ð° ÑÐ½ÑÑ Ð²ÐµÑÑÑÑ Ð³ÑÐ¸ Ð¹ Ð¼Ð¾Ð¶Ðµ ÑÐ¿ÑÐ¸ÑÐ¸Ð½Ð¸ÑÐ¸ Ð·Ð±ÑÐ¹. Â«ÐÑÐ´Ð½Ð¾Ð²Ð¸ÑÐ¸Â» Ð¿Ð¾Ð²ÐµÑÑÐ°Ñ Ð¾ÑÐ¸Ð³ÑÐ½Ð°Ð»ÑÐ½Ñ Ð±Ð°Ð¹ÑÐ¸.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð¿ÑÑÑÐ¸ÑÐ¸.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð¿ÑÑÑÐ¸ÑÐ¸: {msg}","ÐÐ°Ð¿Ð¸Ñ Ð½Ð° Ð·Ð°Ð¿ÑÑÐº Ð½Ðµ Ð²Ð´Ð°Ð²ÑÑ. ÐÑÐ°, Ð¼Ð¾Ð¶Ð»Ð¸Ð²Ð¾, ÑÐµ Ð·Ð°Ð¿ÑÑÐºÐ°ÑÑÑÑÑ.","Ð§Ð°Ñ Ð·Ð°Ð¿ÑÑÐºÑ Ð²Ð¸ÑÐµÑÐ¿Ð°Ð½Ð¾. ÐÐµÑÐµÐ²ÑÑÑÐµ, ÑÐ¸ Ð·Ð°Ð¿ÑÑÑÐ¸Ð»Ð°ÑÑ Ð³ÑÐ°.","ÐÐ°Ð¿ÑÑÐº: {name}...","ÐÑÑÐ½Ð°Ð»Ð¸ ÑÐºÐ¾Ð¿ÑÐ¹Ð¾Ð²Ð°Ð½Ð¾.","ÐÑÐ¾Ð±Ð»ÐµÐ½Ð¾ Ð· {heart} â maj0r","ÐÑÐ´Ð¿Ð¾Ð²ÑÐ´Ð°Ñ v{v}","ÐÐ¾ÑÑÑÐ±Ð½Ð° v{v}","ÐÐµÐ¼Ð°Ñ Ð³ÑÐ¸","ÐÐ¾Ð´Ð½Ð° Ð³ÑÐ° Ð½Ðµ Ð²ÑÐ´Ð¿Ð¾Ð²ÑÐ´Ð°Ñ Ð¿Ð¾ÑÑÐºÑ.","ÐÐ³Ð¾Ñ ÑÐ· ÑÐ¸ÑÐ°Ð¼Ð¸ Ð¿Ð¾ÐºÐ¸ Ð½ÐµÐ¼Ð°Ñ. ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÑÐµ ÑÑ Ñ ÑÐ¾Ð·Ð´ÑÐ»Ñ Â«ÐÐ¶ÐµÑÐµÐ»Ð°Â».","ÐÑÑÐ½Ð°Ð»ÑÐ² Ð¿Ð¾ÐºÐ¸ Ð½ÐµÐ¼Ð°Ñ.","ÐÑÐ´Ð¿Ð¾Ð²ÑÐ´Ð½Ð¸Ñ ÑÑÐ´ÐºÑÐ² Ð½ÐµÐ¼Ð°Ñ.","ÐÐµ Ð³Ð¾ÑÐ¾Ð²Ð¾","ÐÐµÑÐ¾Ð´Ð°Ð²Ð½ÑÑ Ð½ÐµÐ¼Ð°Ñ. ÐÐ°Ð¿ÑÑÑÑÑÑ Ð³ÑÑ Ð°Ð±Ð¾ Ð²ÑÐ´ÐºÑÐ¸Ð¹ÑÐµ ÑÑ ÑÑÐµÐ¹Ð½ÐµÑ.","ÐÑÑÐ¾Ð³Ð¾ ÐºÐ¾Ð¿ÑÑÐ²Ð°ÑÐ¸.","ÐÑÑÐ¾Ð³Ð¾ Ð½Ðµ Ð±ÑÐ»Ð¾ Ð²Ð²ÑÐ¼ÐºÐ½ÐµÐ½Ð¾.","ÐÑÐ´ÐºÑÐ¸ÑÐ¸ ÑÑÐµÐ¹Ð½ÐµÑ: {name}","ÐÐ²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ Ð¿Ð°ÑÑÑÐ² Ð¾ÑÐ¸ÑÐµÐ½Ð¾.","ÐÐ²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ Ð¿Ð°ÑÑÑÐ² Ð·Ð±ÐµÑÐµÐ¶ÐµÐ½Ð¾.","QR-ÐºÐ¾Ð´ Ð½ÐµÐ´Ð¾ÑÑÑÐ¿Ð½Ð¸Ð¹.","Ð§Ð¸ÑÐ°Ð½Ð½Ñ ÑÐ¿Ð¸ÑÐºÑ ÑÐ°Ð¹Ð»ÑÐ² ÑÐ· GitHub","ÐÑÐ¸Ð±ÑÐ°ÑÐ¸ Ð· Ð²Ð¸Ð±ÑÐ°Ð½Ð¾Ð³Ð¾","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ ÑÐºÐ¸Ð½ÑÑÐ¸.","Ð¡ÐºÐ¸Ð½ÑÑÐ¸ Ð½Ð°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½Ñ?","ÐÐ±ÐµÑÐµÐ¶ÐµÐ½Ð¾","ÐÐ±ÐµÑÐµÐ¶ÐµÐ½Ð½Ñ ÑÐ°Ð¹Ð»ÑÐ² ÑÐ¸ÑÑÐ²","ÐÐ±ÐµÑÐµÐ¶ÐµÐ½Ð½Ñ...","ÐÐ¾ÑÑÐ¼ Ð½Ð°Ð´ÑÑÐ»ÑÑÑ Ð½Ð¾Ð²Ð¸Ð¹ CheatRunner.elf Ð· ÐºÐ¾Ð¼Ð¿'ÑÑÐµÑÐ°.","ÐÐ°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½Ñ ÑÐºÐ¸Ð½ÑÑÐ¾.","ÐÐ¸Ð¼ÐºÐ½ÑÑÐ¸","ÐÐ¸Ð¼ÐºÐ½ÑÑÐ¸ CheatRunner?","ÐÐ¶ÐµÑÐµÐ»Ð¾ {i} Ð· {n}","Ð¡Ð¿Ð¾ÑÐ°ÑÐºÑ Ð·Ð°Ð¿ÑÑÑÑÑÑ Ð³ÑÑ.","ÐÑÐ° ÑÐµ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÑÑÑÑÑÑ.","ÐÑÐ° ÑÐµ Ð·Ð°Ð¿ÑÑÐºÐ°ÑÑÑÑÑ. Ð¡Ð¿ÑÐ¾Ð±ÑÐ¹ÑÐµ ÑÐµ ÑÐ°Ð· Ð·Ð° ÐºÑÐ»ÑÐºÐ° ÑÐµÐºÑÐ½Ð´.","Ð¡Ð¿Ð¸ÑÐ¾Ðº ÑÐ³Ð¾Ñ Ð·Ð°ÑÐ°Ð· Ð½ÐµÐ´Ð¾ÑÑÑÐ¿Ð½Ð¸Ð¹.","Ð¦ÐµÐ¹ ÑÐ¸Ñ Ð·Ð°ÑÐ°Ð· Ð½Ðµ Ð¼Ð¾Ð¶Ð½Ð° Ð¿ÐµÑÐµÐ¼ÐºÐ½ÑÑÐ¸.","Ð¦ÐµÐ¹ ÑÐ¸Ñ ÑÐ¶Ðµ ÑÐ¿ÑÐ¸ÑÐ¸Ð½ÑÐ² Ð·Ð±ÑÐ¹ Ð³ÑÐ¸. ÐÐ½ÑÐ¼ÑÑÑ Ð¿Ð¾Ð·Ð½Ð°ÑÐºÑ, ÑÐ¾Ð± ÑÐ¿ÑÐ¾Ð±ÑÐ²Ð°ÑÐ¸ Ð·Ð½Ð¾Ð²Ñ.","Ð¦ÐµÐ¹ ÑÐ°Ð¹Ð» Ð´Ð»Ñ ÑÐ½ÑÐ¾Ñ Ð²ÐµÑÑÑÑ Ð³ÑÐ¸, Ð½ÑÐ¶ Ð²ÑÑÐ°Ð½Ð¾Ð²Ð»ÐµÐ½Ð°.","Ð¤Ð°Ð¹Ð» Ð±ÑÐ´Ðµ Ð²Ð¸Ð´Ð°Ð»ÐµÐ½Ð¾ Ð· ÐºÐ¾Ð½ÑÐ¾Ð»Ñ. ÐÐ¾Ð³Ð¾ Ð¼Ð¾Ð¶Ð½Ð° Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ Ð·Ð½Ð¾Ð²Ñ Ð² ÑÐ¾Ð·Ð´ÑÐ»Ñ Â«ÐÐ¶ÐµÑÐµÐ»Ð°Â».","ÐÐ°Ð¹Ð½ÑÐ»Ð¾ {t}","URL ÑÐºÐ¾Ð¿ÑÐ¹Ð¾Ð²Ð°Ð½Ð¾.","Ð Ð¾Ð·Ð¿Ð°ÐºÑÐ²Ð°Ð½Ð½Ñ: {name}","ÐÐ°ÑÑ Ð½Ðµ Ð¿ÑÐ´ÑÑÐ¸Ð¼ÑÑÑÑÑÑ","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ ÑÐ°Ð¹Ð».","ÐÐ¸ÐºÐ¾ÑÐ¸ÑÑÐ°Ð¹ÑÐµ ÑÐ°Ð¹Ð» .mc4, .shn Ð°Ð±Ð¾ .json.","ÐÐ¸ÐºÐ¾ÑÐ¸ÑÑÐ°ÑÐ¸ ÑÐ½ÑÑ Ð²ÐµÑÑÑÑ?","ÐÑÐµ Ð¾Ð´Ð½Ð¾ Ð²Ð¸ÐºÐ¾ÑÐ¸ÑÑÐ°ÑÐ¸","ÐÑÑÐºÑÐ²Ð°Ð½Ð½Ñ Ð¿Ð¾Ð²ÐµÑÐ½ÐµÐ½Ð½Ñ CheatRunner...","ÐÐ´ÑÐµÑÑ {label} ÑÐºÐ¾Ð¿ÑÐ¹Ð¾Ð²Ð°Ð½Ð¾","ÑÑÑ Ð´Ð¶ÐµÑÐµÐ»Ð°","Ñ ÑÐµÑÐµÐ´Ð½ÑÐ¾Ð¼Ñ {s}","ÑÐ¾Ð¹Ð½Ð¾","ÑÐ°Ð¹Ð» ÑÐ¸ÑÑÐ²","v{v} Ð¿ÑÐ´ÑÐ¾Ð´Ð¸ÑÑ","Ð§ÑÑÑÐ² Ð²Ð¸Ð¼ÐºÐ½ÐµÐ½Ð¾: {n}.","ÐÐ±ÐµÑÐµÐ¶ÐµÐ½Ð¾ Ð½Ð¾Ð²Ð¸Ñ Ð°Ð±Ð¾ Ð¾Ð½Ð¾Ð²Ð»ÐµÐ½Ð¸Ñ ÑÐ°Ð¹Ð»ÑÐ²: {n}","ÐÐ³Ð¾Ñ: {n}","ÐÐ³Ð¾Ñ: {n}","{n} Ð´Ð½. ÑÐ¾Ð¼Ñ","{n} Ð³Ð¾Ð´ ÑÐ¾Ð¼Ñ","{n} ÑÐ² ÑÐ¾Ð¼Ñ","{page} - CheatRunner","Ð£Ð²ÑÐ¼ÐºÐ½ÐµÐ½Ð¾ {a} Ð· {n}","{h} Ð³Ð¾Ð´ {m} ÑÐ²","{m} ÑÐ²","{s} Ñ","{m} ÑÐ² {s} Ñ","Ð°Ð²ÑÐ¾Ñ: {a}","ÐÐ°ÑÐºÐ°Ð²Ð¾ Ð¿ÑÐ¾ÑÐ¸Ð¼Ð¾ Ð´Ð¾ CheatRunner.","Ð¤Ð°Ð¹Ð»ÑÐ² ÑÐ¸ÑÑÐ² Ð¿Ð¾ÐºÐ¸ Ð½ÐµÐ¼Ð°Ñ. ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÑÐµ ÑÐ¸ÑÐ¸ ÑÐ¿ÑÐ»ÑÐ½Ð¾ÑÐ¸ Ð² Â«ÐÐ¶ÐµÑÐµÐ»Ð°ÑÂ» Ð°Ð±Ð¾ Ð²ÑÐ´ÐºÑÐ¸Ð¹ÑÐµ Ð³ÑÑ Ð¹ Ð¿ÐµÑÐµÑÑÐ³Ð½ÑÑÑ ÑÐ°Ð¹Ð».","Ð¡ÐºÐ°ÑÑÐ²Ð°ÑÐ¸","ÐÑÐ¾Ð´Ð¾Ð²Ð¶Ð¸ÑÐ¸","ÐÐµÑÐµÐ¿ÑÐ´ÐºÐ»ÑÑÐµÐ½Ð½Ñ...","Ð¡ÐºÐ¾Ð¿ÑÐ¹Ð¾Ð²Ð°Ð½Ð¾","Ð¡ÐºÐ¾Ð¿ÑÐ¹Ð¾Ð²Ð°Ð½Ð¾.","ÐÐµÐ²ÑÐ´Ð¾Ð¼Ð¾","ÐÑÐ°Ð»Ð¾Ð³","ÐÐ¸Ð¼Ðº.","ÐÑÐ¸Ð²ÑÐ»ÐµÑ","OK","ÐÑÐ´ÑÑÑÐ½Ñ","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ.","ÐÐ°Ð¿ÑÑÐº","Ð¡Ð¿ÑÐ¾Ð±ÑÐ²Ð°ÑÐ¸ ÑÐµ ÑÐ°Ð·","ÐÑÐ°","ÐÐ¾ÑÑÐº ÑÐ¸ÑÑÐ²","ÐÑÐ´Ð¿Ð¸ÑÑÐ¹ÑÐµÑÑ Ð½Ð° maj0r Ñ X","ÐÑÐ´Ð¿Ð¸ÑÑÐ¹ÑÐµÑÑ Ð½Ð° maj0r Ñ Bluesky","ÐÐ²ÑÐ¾Ñ: maj0r - ð§ð·","ÐÐ¾ÑÑÐ°Ð²ÑÐµ Ð¿ÑÐ¾ÑÐºÑÑ Ð·ÑÑÐºÑ! â­","ÐÐ°Ð»Ð¸Ð½Ð¾Ð²Ð°","ÐÐ²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ","Ð¡ÐºÐ¸Ð½ÑÑÐ¸","ÐÐ»Ñ ÑÑÑÑ Ð³ÑÐ¸ Ð½ÐµÐ¼Ð°Ñ Ð¿Ð°ÑÑÑÐ².","ÐÐ¾Ð¼ÑÑÑÑÑÑ ÑÐ²Ð¾Ñ XML-Ð¿Ð°ÑÑÑ Ð² /data/cheatrunner/patches/xml","ÐÐ¾ÑÑÑÐ¿Ð½Ðµ Ð¾Ð½Ð¾Ð²Ð»ÐµÐ½Ð½Ñ","ÐÐ¾ÑÑÑÐ¿Ð½Ð° Ð²ÐµÑÑÑÑ {v}.","Ð¨ÑÐºÐ°ÑÐ¸ ÑÐ¸ÑÐ¸ Ð¾Ð½Ð»Ð°Ð¹Ð½","ÐÐ½Ð°Ð¹Ð´ÑÑÑ ÑÑÑ ÑÐ³ÑÐ¸ Ð· ÑÐ¸ÑÐ°Ð¼Ð¸ ÑÐ° Ð¿ÐµÑÐµÐ²ÑÑÑÐµ, ÑÐ¸ Ñ Ð²Ð°ÑÐ°.","ÐÑÐ´ÐºÑÐ¸ÑÐ¸ ÑÐ°Ð¹Ñ","Ð ÐµÐ¿Ð¾Ð·Ð¸ÑÐ¾ÑÑÑ Ð¿Ð°ÑÑÑÐ² ÑÐ¿ÑÐ»ÑÐ½Ð¾ÑÐ¸","ÐÐ¾ÑÐ¾Ð²Ñ Ð¿Ð°ÑÑÑ Ð´Ð»Ñ Ð²Ð°ÑÐ¸Ñ ÑÐ³Ð¾Ñ. ÐÑÑÐ¾Ð³Ð¾ Ð½Ðµ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÑÑÑÑÑÑ â ÑÐµÐ¿Ð¾Ð·Ð¸ÑÐ¾ÑÑÐ¹ Ð²ÑÐ´ÐºÑÐ¸ÑÑÑÑÑ Ð² Ð½Ð¾Ð²ÑÐ¹ Ð²ÐºÐ»Ð°Ð´ÑÑ.","ÐÑÐ´ÐºÑÐ¸ÑÐ¸ PS-Game-Patch","ÐÑÐ·ÑÐ°Ð»ÑÐ½Ñ ÐµÑÐµÐºÑÐ¸","Ð£ ÑÐµÐ¶Ð¸Ð¼Ñ Â«ÐÐ½Ð¸Ð¶ÐµÐ½ÑÂ» Ð²Ð¸Ð¼Ð¸ÐºÐ°ÑÑÑÑÑ ÑÐ¾Ð·Ð¼Ð¸ÑÑÑ, ÑÑÐ½Ñ ÑÐ° Ð°Ð½ÑÐ¼Ð°ÑÑÑ. Ð£ Ð±ÑÐ°ÑÐ·ÐµÑÑ PS5 Ð²ÑÐ½ ÑÐ²ÑÐ¼ÐºÐ½ÐµÐ½Ð¸Ð¹ Ð·Ð° Ð·Ð°Ð¼Ð¾Ð²ÑÑÐ²Ð°Ð½Ð½ÑÐ¼.","ÐÐ¾Ð²Ð½Ñ","ÐÐ½Ð¸Ð¶ÐµÐ½Ñ","ÐÐ¾ÐºÐ°Ð·Ð°ÑÐ¸ ÑÐµ ({n})","Ð ÐµÐºÐ¾Ð¼ÐµÐ½Ð´Ð¾Ð²Ð°Ð½Ð¾","ÐÑÑÑÑÐ¹Ð½Ð¸Ð¹ ÑÐµÐ»ÑÐ· PS-Game-Patch. ÐÐ°ÑÑÑ Ð·Ð±ÐµÑÑÐ³Ð°ÑÑÑÑÑ Ð² /data/cheatrunner/patches.","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ Ð¿Ð°ÑÑÑ","ÐÐ±ÐµÑÐµÐ¶ÐµÐ½Ð½Ñ ÑÐ°Ð¹Ð»ÑÐ² Ð¿Ð°ÑÑÑÐ²","ÐÐµÑÐºÑ Ð¿Ð°ÑÑÑ Ð¿ÑÐ°ÑÑÑÑÑ Ð»Ð¸ÑÐµ Ð·Ð° ÑÐ¼Ð¾Ð²Ð¸ Ð·Ð°ÑÑÐ¾ÑÑÐ²Ð°Ð½Ð½Ñ Ð½Ð° Ð¿Ð¾ÑÐ°ÑÐºÑ Ð³ÑÐ¸: Ð·Ð±ÐµÑÐµÐ¶ÑÑÑ Ð°Ð²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ ÑÐ° Ð¿ÐµÑÐµÐ·Ð°Ð¿ÑÑÑÑÑÑ Ð³ÑÑ.","Ð£ÑÑÐ½ÐµÐ½Ð½Ñ Ð½ÐµÑÐ¿ÑÐ°Ð²Ð½Ð¾ÑÑÐµÐ¹","ÐÐ¾Ð²ÑÐ´Ð¾Ð¼ÑÐµ Ð¿ÑÐ¾ Ð¿ÑÐ¾Ð±Ð»ÐµÐ¼Ñ Ð°Ð±Ð¾ Ð¿Ð¾Ð²ÐµÑÐ½ÑÑÑ ÑÑÑ Ð½Ð°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½Ñ Ð´Ð¾ ÑÐ¸Ð¿Ð¾Ð²Ð¸Ñ.","ÐÐ¾Ð¿ÑÑÑ Ð´ÑÐ°Ð³Ð½Ð¾ÑÑÐ¸ÐºÑ ÑÐ° Ð²ÑÐ´ÐºÑÐ¸Ð²Ð°Ñ ÑÑÐ¾ÑÑÐ½ÐºÑ Ð·Ð²ÐµÑÐ½ÐµÐ½Ñ Ð½Ð° GitHub.","Ð§Ð¸ÑÐ¸, Ð¿Ð°ÑÑÑ ÑÐ° Ð¿ÑÐ¾ÑÑÐ»Ñ Ð°Ð²ÑÐ¾Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ Ð½Ðµ Ð·Ð¼ÑÐ½ÑÑÑÑÑÑ.","Ð£ÑÐµ Ð²Ð¸Ð³Ð»ÑÐ´Ð°Ñ Ð´Ð¾Ð±ÑÐµ.","ÐÐµÑÐ¾ Ð¿Ð¾ÑÑÐµÐ±ÑÑ ÑÐ²Ð°Ð³Ð¸ - Ð¿ÐµÑÐµÐ³Ð»ÑÐ½ÑÑÐµ Ð¿Ð¾Ð´ÑÐ¾Ð±Ð¸ÑÑ Ð½Ð¸Ð¶ÑÐµ.","Ð¤Ð°Ð¹Ð» .mc4, .shn Ð°Ð±Ð¾ .json Ð· Ð²Ð°ÑÐ¾Ð³Ð¾ ÐºÐ¾Ð¼Ð¿âÑÑÐµÑÐ°.","ÐÐ¾Ð´Ð°Ð¹ÑÐµ ÑÐ°Ð¹Ð» Ñ Â«Ð¤Ð°Ð¹Ð»Ð¸Â» Ð°Ð±Ð¾ Â«ÐÐ¶ÐµÑÐµÐ»Ð°Â»","ÐÐ¸Ð³Ð»ÑÐ´","ÐÐ¸Ð³Ð»ÑÐ´, ÑÑÑÑÐ¹ ÑÐ¸ÑÑÐ² Ñ Ð³ÐµÐ¹Ð¼Ð¿Ð°Ð´. ÐÐ¼ÑÐ½Ð¸ Ð·Ð±ÐµÑÑÐ³Ð°ÑÑÑÑÑ Ð¾Ð´ÑÐ°Ð·Ñ.","Ð¤Ð°Ð¹Ð» ÑÐ¸ÑÑÐ²","Ð¤Ð°Ð¹Ð»Ð¸ ÑÐ¸ÑÑÐ² Ð´Ð»Ñ ÑÑÑÑ Ð³ÑÐ¸","CheatRunner Ð¿Ð¾ÐºÐ°Ð·ÑÑ ÑÐ³ÑÐ¸, Ð²ÑÑÐ°Ð½Ð¾Ð²Ð»ÐµÐ½Ñ Ð½Ð° ÑÑÐ¹ ÐºÐ¾Ð½ÑÐ¾Ð»Ñ. ÐÑÑÐ°Ð½Ð¾Ð²ÑÑÑ Ð³ÑÑ ÑÐ° Ð¾Ð½Ð¾Ð²ÑÑÑ ÑÐ¿Ð¸ÑÐ¾Ðº.","Ð§Ð¸ÑÐ¸ Ð¹ Ð¿Ð°ÑÑÑ Ð½Ðµ Ð¼Ð¾Ð¶Ð½Ð° Ð·Ð°ÑÑÐ¾ÑÐ¾Ð²ÑÐ²Ð°ÑÐ¸, Ð´Ð¾ÐºÐ¸ Ð¹Ð¾Ð³Ð¾ Ð½Ðµ Ð²Ð²ÑÐ¼ÐºÐ½ÐµÐ½Ð¾.","ÐÐµÑÐµÐ²ÑÑÐºÐ°...","ÐÐ¸Ð±ÑÐ°ÑÐ¸ ÑÐ½ÑÐ¸Ð¹ ÑÐ°Ð¹Ð»","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ ÑÐ°Ð¹Ð» ÑÐ¸ÑÑÐ²","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ Ð¿Ð°ÑÑÑ","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ Ð½Ð°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½Ñ","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶Ð¸ÑÐ¸ Ð²Ð°ÑÑ ÑÐ³ÑÐ¸","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð¾ÑÑÐ¸Ð¼Ð°ÑÐ¸ Ð¿Ð¾ÑÐ¾ÑÐ½Ð¸Ð¹ ÑÑÐ°Ð½ ÑÐ¸ÑÑÐ².","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÑÐµ ÑÐ¸ÑÐ¸ ÑÐ¿ÑÐ»ÑÐ½Ð¾ÑÐ¸ Ð² Â«ÐÐ¶ÐµÑÐµÐ»Ð°ÑÂ» Ð°Ð±Ð¾ Ð´Ð¾Ð´Ð°Ð¹ÑÐµ ÑÐ°Ð¹Ð» ÑÐ· ÐºÐ¾Ð¼Ð¿âÑÑÐµÑÐ°.","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÑÐµ ÑÐ°Ð¹Ð» Ñ Â«ÐÐ¶ÐµÑÐµÐ»Ð°ÑÂ» Ð°Ð±Ð¾ Ð¿ÐµÑÐµÑÑÐ³Ð½ÑÑÑ Ð¹Ð¾Ð³Ð¾ Ð½Ð¸Ð¶ÑÐµ.","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÑÐ¹ÑÐµ Ð³Ð¾ÑÐ¾Ð²Ñ ÑÐ¸ÑÐ¸ Ð¹ Ð¿Ð°ÑÑÑ Ð´Ð»Ñ Ð²Ð°ÑÐ¸Ñ ÑÐ³Ð¾Ñ. ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ ÑÑÐ¸Ð²Ð°Ñ, Ð¿Ð¾ÐºÐ¸ Ð²Ð¸ ÐºÐ¾ÑÐ¸ÑÑÑÑÑÐµÑÑ Ð¿Ð°Ð½ÐµÐ»Ð»Ñ.","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÑÐµ Ð¾ÑÑÑÑÐ¹Ð½Ð¸Ð¹ Ð½Ð°Ð±ÑÑ Ð¿Ð°ÑÑÑÐ² Ñ Â«ÐÐ¶ÐµÑÐµÐ»Ð°ÑÂ» Ð°Ð±Ð¾ Ð¿Ð¾ÐºÐ»Ð°Ð´ÑÑÑ ÑÐ²Ð¾Ñ XML-Ð¿Ð°ÑÑÑ Ð² /data/cheatrunner/patches/xml.","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ {p}%","ÐÐ°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð½Ñ","Ð ÑÑÑÐ¹","ÐÐ¾ÐºÐ°Ð·Ð°Ð½Ð¾ Ð¾ÑÑÐ°Ð½Ð½Ñ ÑÑÐ´ÐºÐ¸","ÐÑÑÐ¸Ð¼Ð°ÑÐ¸ ÑÐ¸ÑÐ¸","Ð¡ÑÐ°Ð½","ÐÑÐ±Ð»ÑÐ¾ÑÐµÐºÐ°","ÐÐ¾ÑÐ¾ÑÐ½Ð¸Ð¹ ÑÑÐ°Ð½ ÑÑÑÑÑ ÑÐ¸ÑÑÐ² Ñ Ð·Ð°Ð¿Ð¸ÑÑ Ð² Ð¿Ð°Ð¼âÑÑÑ, Ð° ÑÐ°ÐºÐ¾Ð¶ ÑÐ¾ ÑÐ¾Ð±Ð¸Ð² CheatRunner.","ÐÐ¾Ð²Ð¸Ð½Ð¸, Ð´Ð¾Ð¿Ð¾Ð¼Ð¾Ð³Ð° ÑÐ° Ð»ÑÐ´Ð¸, ÑÐºÑ ÑÑÐ¾ÑÑÑ Ð·Ð° CheatRunner.","ÐÐ»Ñ ÑÑÑÑ Ð³ÑÐ¸ ÑÐµ Ð½ÐµÐ¼Ð°Ñ ÑÐ°Ð¹Ð»Ñ ÑÐ¸ÑÑÐ²","ÐÐµÐ¼Ð°Ñ ÑÐ¸ÑÑÐ² Ð·Ð° Ð·Ð°Ð¿Ð¸ÑÐ¾Ð¼ Â«{q}Â».","Ð¤Ð°Ð¹Ð»Ñ ÑÐµ Ð½ÐµÐ¼Ð°Ñ","ÐÐ³Ð¾Ñ Ð¿Ð¾ÐºÐ¸ Ð½Ðµ Ð·Ð½Ð°Ð¹Ð´ÐµÐ½Ð¾","ÐÐ¾ÐºÐ°Ð»ÑÐ½Ð¸Ð¹ ÑÐ°Ð¹Ð» Ð½Ðµ Ð·Ð½Ð°Ð¹Ð´ÐµÐ½Ð¾","ÐÐ»Ñ ÑÑÑÑ Ð³ÑÐ¸ Ð½ÐµÐ¼Ð°Ñ Ð¿Ð°ÑÑÑÐ²","ÐÐ°ÑÐ°Ð· Ð·Ð°Ð¿ÑÑÐµÐ½Ð°","ÐÑÐ´ÐºÑÐ¸ÑÐ¸ Ð½Ð°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½Ñ","ÐÑÐ¸Ð·ÑÐ¿Ð¸Ð½ÐµÐ½Ð¾, Ð¿Ð¾ÐºÐ¸ Ð²Ð¸ ÑÐ¸ÑÐ°ÑÑÐµ","Ð£Ð¿Ð¾Ð´Ð¾Ð±Ð°Ð½Ð½Ñ","Â«ÐÐ½Ð¸Ð¶ÐµÐ½ÑÂ» Ð²Ð¸Ð¼Ð¸ÐºÐ°ÑÑÑ ÑÑÑ Ð°Ð½ÑÐ¼Ð°ÑÑÑ. Ð¦Ðµ ÑÐ¸Ð¿Ð¾Ð²Ðµ Ð·Ð½Ð°ÑÐµÐ½Ð½Ñ Ð² Ð±ÑÐ°ÑÐ·ÐµÑÑ PS5.","ÐÐ±ÐµÑÑÐ³Ð°ÑÑÑÑÑ Ð² /data/cheatrunner/cheats. Ð¢ÑÐµÐ¹Ð½ÐµÑ ÑÐ¸ÑÐ°Ñ ÑÐ°Ð¹Ð» ÑÐ· Ð¿Ð¾Ð·Ð½Ð°ÑÐºÐ¾Ñ Â«ÐÐ¸ÐºÐ¾ÑÐ¸ÑÑÐ¾Ð²ÑÑÑÑÑÑÂ».","ÐÐ°Ð´ÑÑÐ»ÑÑÑ Ð·Ð²ÑÑ, ÑÐºÑÐ¾ ÑÐ¾ÑÑ Ð½Ðµ Ð¿ÑÐ°ÑÑÑ.","ÐÐ°Ð¿ÑÑÑÑÑÑ, ÑÐ¾Ð± ÐºÐ¾ÑÐ¸ÑÑÑÐ²Ð°ÑÐ¸ÑÑ ÑÐ¸ÑÐ°Ð¼Ð¸","ÐÐ°Ð¿ÑÑÑÑÑÑ ÑÑ Ð³ÑÑ, ÑÐ¾Ð± ÐºÐ¾ÑÐ¸ÑÑÑÐ²Ð°ÑÐ¸ÑÑ ÑÑ ÑÐ¸ÑÐ°Ð¼Ð¸.","Ð¢ÐµÑÐ½ÑÑÐ½Ñ Ð¿Ð¾Ð´ÑÐ¾Ð±Ð¸ÑÑ","Ð ÑÑÑÐ¹ ÑÐ¸ÑÑÐ² Ð²Ð¸Ð¼ÐºÐ½ÐµÐ½Ð¾.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð¿ÑÐ¾ÑÐ¸ÑÐ°ÑÐ¸ Ð¿Ð¾ÑÐ¾ÑÐ½Ñ Ð·Ð½Ð°ÑÐµÐ½Ð½Ñ, ÑÐ¾Ð¼Ñ Ð½ÑÑÐ¾Ð³Ð¾ Ð½Ðµ Ð¿Ð¾ÐºÐ°Ð·Ð°Ð½Ð¾, ÑÐ¾Ð± Ð½Ðµ Ð²Ð²Ð¾Ð´Ð¸ÑÐ¸ Ð² Ð¾Ð¼Ð°Ð½Ñ.","ÐÑÐ½Ð¾Ð²Ð½Ðµ Ð´Ð»Ñ Ð·Ð°ÑÑÐ¾ÑÑÐ²Ð°Ð½Ð½Ñ ÑÐ¸ÑÑÐ² Ñ Ð¿Ð°ÑÑÑÐ².","Ð¤Ð°Ð¹Ð» Ñ, Ð°Ð»Ðµ Ð¹Ð¾Ð³Ð¾ Ð½Ðµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð¿ÑÐ¾ÑÐ¸ÑÐ°ÑÐ¸. ÐÑÐ½ Ð¼Ð¾Ð¶Ðµ Ð±ÑÑÐ¸ Ð¿Ð¾ÑÐºÐ¾Ð´Ð¶ÐµÐ½Ð¸Ð¹ Ð°Ð±Ð¾ ÑÐµ ÐºÐ¾Ð¿ÑÑÑÑÑÑÑ.","Ð¤Ð°Ð¹Ð» Ð·Ð°Ð²Ð°Ð½ÑÐ°Ð¶ÐµÐ½Ð¾, Ð°Ð»Ðµ Ð²ÑÐ½ Ð¿Ð¾ÑÐ¾Ð¶Ð½ÑÐ¹. Ð¡Ð¿ÑÐ¾Ð±ÑÐ¹ÑÐµ ÑÐ½ÑÐ¸Ð¹ ÑÐ°Ð¹Ð» Ð´Ð»Ñ ÑÑÑÑ Ð³ÑÐ¸.","Ð¡Ð¿Ð¸ÑÐ¾Ðº ÑÐ³Ð¾Ñ Ð·Ð°ÑÐ°Ð· Ð½ÐµÐ´Ð¾ÑÑÑÐ¿Ð½Ð¸Ð¹. ÐÐµÑÐµÐºÐ¾Ð½Ð°Ð¹ÑÐµÑÑ, ÑÐ¾ ÐºÐ¾Ð½ÑÐ¾Ð»Ñ ÑÐ²ÑÐ¼ÐºÐ½ÐµÐ½Ð°, Ñ ÑÐ¿ÑÐ¾Ð±ÑÐ¹ÑÐµ ÑÐµ ÑÐ°Ð·.","ÐÐµ Ð²Ð´Ð°Ð»Ð¾ÑÑ Ð¿ÑÐ¾ÑÐ¸ÑÐ°ÑÐ¸ ÑÐ°Ð¹Ð»Ð¸ Ð¿Ð°ÑÑÑÐ² Ð´Ð»Ñ ÑÑÑÑ Ð³ÑÐ¸. Ð¡Ð¿ÑÐ¾Ð±ÑÐ¹ÑÐµ Ð·Ð° Ð¼Ð¸ÑÑ.","ÐÐµÑÐµÐ¼Ð¸ÐºÐ°ÑÑ Ð¼Ð¾Ð¶ÑÑÑ Ð±ÑÑÐ¸ Ð·Ð°ÑÑÐ°ÑÑÐ»Ð¸Ð¼Ð¸.","Ð¢ÐµÐ¼Ð°, ÐµÑÐµÐºÑÐ¸ ÑÐ° Ð¼Ð¾Ð²Ð° ÑÑÑÑ Ð¿Ð°Ð½ÐµÐ»Ñ.","Ð£ ÑÑÐ¾Ð¼Ñ ÑÐ°Ð¹Ð»Ñ Ð½ÐµÐ¼Ð°Ñ ÑÐ¸ÑÑÐ²","Ð¡Ð¿ÑÐ¾Ð±ÑÐ¹ÑÐµ Ð½Ð¸Ð¶ÑÐµ","Ð£Ð²ÑÐ¼ÐºÐ½ÑÑÑ Ñ Ð½Ð°Ð»Ð°ÑÑÑÐ²Ð°Ð½Ð½ÑÑ","ÐÐµÐ±ÐµÐ·Ð¿ÐµÑÐ½Ð° Ð·Ð¾Ð½Ð°","ÐÐ¾ Ð¾ÑÑÐ°Ð½Ð½ÑÑ","ÐÐ¸Ð´Ð°Ð»Ð¸ÑÐ¸ ÑÐ°Ð¹Ð», ÑÐ¾ Ð²Ð¸ÐºÐ¾ÑÐ¸ÑÑÐ¾Ð²ÑÑÑÑÑÑ","ÐÐ¶ÐµÑÐµÐ»Ð¾ ÑÐ¸ÑÑÐ²","Ð¤ÑÐ»ÑÑÑ ÑÐ³Ð¾Ñ","ÐÐµÑÐµÐ³Ð»ÑÐ½ÑÑÐ¸ ÑÐ³ÑÐ¸","ÐÐ²ÑÐ¾ÑÐ¸","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"],"zh-CN":["ä¸æ¬¡æä¸ä¸ªä½å¼é¡¹å¯¼è´æ¸¸æå´©æºã","å³äº","æä½","æä½","æ·»å æä»¶","æ·»å ä½å¼","æ·»å å°æ¶è","å¨æ¯ä¸ªæ¸¸æçéé¡¹èåä¸­æ·»å  â CheatRunner å¥å£ï¼å¹¶æä¾å¯æå¼å½åè¿è¡æ¸¸æä¿®æ¹å¨çæé®ç»åã","é«çº§","å¨é¨","ææèµå©é½å°ç¨äºè´­ä¹°ä½åºä»¶çæ¬ï¼1.xxâ4.xxï¼ç PS5ï¼ç¨äºç ç©¶ãå¼åååç»­åçº§ã","ææä¸è½½åå¤±è´¥","æææ¸¸æ","åæ¶æ´æ°å·²æ´æ¹çæä»¶","å·²åºç¨","åºç¨","åºç¨","èªå¨éæ©","è¿å","æµè§ä½ çæ¸¸æåº","æé®","Buy Me a Coffee","ä½å¼","ä½å¼å¼æ","CheatRunner - ä½ ç PS5 ä½å¼ä¿®æ¹å¨ï¼","ä½å¼","æ¸¸æå¯å¨åï¼ä½å¼é¡¹ä¼å»¶è¿è¿ä¹é¿æ¶é´åçæã","æä½å¼çä¼å","éæ©ç½ç»","Circle","æ¸é¤ç­é","æ¸é¤æ è®°","æ¸é¤æç´¢","å³é­","æ¶èµ·","æ¶èµ·èå","æ­¤é¢æ¿çéè²æ¹æ¡ã","ç¤¾åº","ç¤¾åºä½å¼ä»åº","ææå¿«æ·é®ä¸æ¸¸æèå","å¤å¶","å¤å¶ç½å","å¨é¨å¤å¶","å¤å¶ä½å¼æä»¶å¤¹","å¤å¶é¾æ¥","å´©æºæ£æµ","ä¸æ¬¡å¯¼è´æ¸¸æå´©æº","Cross","å å¯è´§å¸","å å¯è´§å¸èµå©","æ·±è²","å é¤","å¨é¨å³é­","ä¸","ä¸è½½æææ¥æº","ä¸è½½ç¤¾åºä½å¼","ä¸è½½å¤±è´¥","ä¸ºä½ çæ¸¸æä¸è½½ç°æçä½å¼æä»¶ãæä»¶ä¿å­å¨ /data/cheatrunner/cheatsï¼ä¸è½½ä¼å¨åå°ç»§ç»­è¿è¡ã","ä¸è½½æé","å·²ä¸è½½","å° .mc4ã.shn æ .json æä»¶æå°æ­¤å¤ï¼æç¹å»æµè§","å¯ç¨ææå¿«æ·é®","å¯ç¨å¿«æ·é®","å¼æå·²å¼å¯","åè½¦æå¼","ä»éè¯¯","ç²¾ç¡®","å¤±è´¥","æ¶è","æä»¶","ç­é","ç­éæ¥å¿","å¦æå¼å¯ä½å¼åæ¸¸æç«å³å³é­ï¼åæ è®°è¯¥ä½å¼é¡¹ã","ä¸ºç¨æ·èçï¼ç±ç¨æ·å±å»ºã","å¼ºå¶åºç¨","æ¸¸æ","æä½å¼çæ¸¸æ","æè¡¥ä¸çæ¸¸æ","ä½å¼ä¹é´çé´éï¼æ¯«ç§ï¼","å¸¸è§","åæ¶æä½ä»¥è§¦åå¿«æ·é®ã","æä½æ¶é´ï¼æ¯«ç§ï¼","åæ¶æä½ä¸¤ä¸ªæé®ï¼å³å¯æå¼å½åè¿è¡æ¸¸æçä¿®æ¹å¨ã","ä¸»å±å¹å¾å","ä¼æé©ç³»ç»çé¢ãé»è®¤å³é­ââå¨é¨ååºä»¶ä¸å¯è½å¯¼è´ä¸»å±å¹å´©æºã","ç»åé®éæä½å¤ä¹æä¼è§¦åã","æ¥å¿è®°å½çè¯¦ç»ç¨åº¦ã","å¦æ CheatRunner å¯¹ä½ æå¸®å©ï¼æ¬¢è¿æ¯æé¡¹ç®çå¼åï¼ð«¶","ä½¿ç¨ä¸­","å¯å¨æ¶å¨ PS5 ä¸»å±å¹å®è£ CheatRunner å¾åã","å å¥ Discord","Ko-Fiãåå¡ãå å¯è´§å¸ãPIX","è¯­è¨","æ­¤é¢æ¿çè¯­è¨ã","ä¸æ¬¡æ¸¸ç©","å·¦","æµè²","ä½å¼å¼æä¸åå­è¡¥ä¸çå®æ¶ç¶æã","æ¬å°ä½å¼æä»¶","æ¥å¿çº§å«","æ¥å¿çº§å«ç­é","æ¥å¿","ä¸»è¦","åºç¨ä½å¼åè¡¥ä¸çæ»å¼å³ã","ä¸»ä»£ç ä¿®æ­£","å¯è½ä¸æ­¤æ¸¸æçæ¬ä¸å¹é","åå­è¡¥ä¸","åå¤","ä¸¤æ¬¡åæ¢ä½å¼ä¹é´çæç­é´éã","åç§° A-Z","åç§° Z-A","æ­¤æ¸¸æææ ä½å¼æä»¶ã","è¿æ²¡ææ¶èãç¹ææ¸¸æä¸çææ å³å¯æ·»å ã","æªè¿è¡","å¼","å¨ææºä¸æå¼","å¨ææºä¸æå¼ï¼äºç»´ç ï¼","å¨ææºä¸æå¼ä»ªè¡¨æ¿","æå¼ä¿®æ¹å¨","éé¡¹","å¶ä»çæ¬","é¡µé¢","è¡¥ä¸","å¼å§æ¸¸æ","äºç»´ç ","ä»ªè¡¨æ¿å°åçäºç»´ç ","æ¯æ¬¡å¯å¨æ­¤æ¸¸ææ¶éæ°åºç¨å·²å¼å¯çä½å¼","å°±ç»ª","æè¿","å·æ°","å·æ°æ¸¸æåè¡¨","éæ°å¯å¨","æ¥åé®é¢","éæ°æ«æå·²å®è£çæ¸¸æ","æ¢å¤é»è®¤è®¾ç½®","èªå¨å¤çä¾èµä¸»ä»£ç çä½å¼é¡¹ã","è¿å","å³","è¿è¡ä¸­","æ­£å¨è¿è¡","ä¿å­èªå¨å è½½","ç¨ææºç¸æºæ«æãææºéä¸ PS5 å¤äºåä¸ Wi-Fi ç½ç»ã","æç´¢","æåç§°ææ é¢ ID æç´¢","æç´¢æ¸¸æ","æç´¢æ¸¸æãé¡µé¢åæä½","éæ©ä¸ä¸ªå°åå³å¯å®æ´å¤å¶ã","è®¾ç½®","æ¾ç¤ºäºç»´ç ","å³é­ payload","æ¸¸ææåº","æºç ","æ¥æº","éåº¦","Square","è¯·åå¯å¨æ¸¸æ","è¯·åå¯å¨æ­¤æ¸¸æååºç¨è¡¥ä¸ã","ç¶æ","ç¶æä¸æ¥å¿","æ¯æ CheatRunner","æ¥èªå·´è¥¿çèµå©","æ¯ææ¬é¡¹ç®","èµå©è","ç³»ç»","åºæ¬è®¾ç½®ââä¸»é¢ä¸ä½å¼å¼æå¼å³ã","ä¸»é¢","ä¸»é¢ä¸ä½å¼å¼æ","å©ä½æ¶é´","æ¸¸ç©æ¶é¿","æ é¢ ID","å¼å³ç±»","TouchPad","ä¿®æ¹å¨","Triangle","ä¸","å·²æ¯ææ°","ä½¿ç¨","çæ¬","æ¸¸æå¯å¨åç­å¾ï¼æ¯«ç§ï¼","è­¦ååéè¯¯","CheatRunner çè¿è¡è®°å½ââåºé®é¢æ¶å¾æç¨ã","æä½å¼","ââ å¯¼èª","ææè®¾ç½®å°æ¢å¤é»è®¤å¼ï¼ç½é¡µç«¯å£ä¿æä¸åã","æ­£å¨åºç¨å¦ä¸ä¸ªä½å¼é¡¹ï¼è¯·ç¨ååè¯ã","ä»»æçæ¬","ä»ç¶åºç¨","å·²æ¢å¤èªå¨éæ©ã","å·²æ¸é¤èªå¨å è½½ã","å·²ä¿å­èªå¨å è½½ï¼{n}ï¼ãæ¸¸æå¯å¨æ¶ä¼èªå¨å¼å¯ã","å·²ä¿å­ç©ºçèªå¨å è½½ã","ä½å¼ #{n}","å·²æ·»å ä½å¼æä»¶ã","å·²å é¤ä½å¼æä»¶ã","å·²éæ©ä½å¼æä»¶ã","å¯å¨æé´ CheatRunner æ²¡æååºã","å·²å¤å¶ä½å¼æä»¶å¤¹ã","æ­£å¨æ£æ¥ {name}","æ¸é¤èªå¨å è½½ï¼{n}ï¼","æ­£å¨éä¸ªæ¯è¾å¹¶ä¸è½½æä»¶","ä¸å¦ä¸ä¸ªä½å¼é¡¹å²çª","å¤å¶å¤±è´¥ï¼è¯·æå¨éæ©ææ¬ã","æ æ³æ¸é¤èªå¨å è½½ã","æ æ³å¤å¶è¯æ­ä¿¡æ¯ã","æ æ³å³é­ä½å¼ã","æ æ³å è½½è¡¥ä¸ã","æ æ³å è½½æ¸¸æåè¡¨ã","æ æ³ä¿å­èªå¨å è½½ã","æ æ³ä¿å­ï¼è¯·éè¯ã","æ æ³éæ©è¯¥æä»¶ã","æ æ³å¼å§ä¸è½½ã","æ æ³å¯å¨ã","æ æ³åæ¢è¯¥ä½å¼é¡¹ã","å é¤å¤±è´¥ã","å é¤æä»¶","å é¤ {file}ï¼","å·²å¤å¶è¯æ­ä¿¡æ¯ï¼è¯·ç²è´´å°é®é¢åé¦ä¸­ã","æ­£å¨ä¸è½½ {name}","å¼æå·²å³é­","å·²å¨é¨æ¯ææ°","å±å¼èå","æ­£å¨ä¸æ¬¡æ§è·åæ´ä¸ªä»åº","æä»¶ v{a} / æ¸¸æ v{b}","{name} å·²å®æ","å¼ºå¶åºç¨ {name}ï¼","æ¸¸æå·²å¯å¨ã","å®éå¯¹å¦ä¸ä¸ªæ¸¸æçæ¬ï¼å¯è½å¯¼è´æ¸¸æå´©æºãâè¿åâä¼æ¢å¤åå§å­èã","å¯å¨å¤±è´¥ã","å¯å¨å¤±è´¥ï¼{msg}","å¯å¨è¯·æ±å¤±è´¥ï¼æ¸¸æå¯è½ä»å¨å¯å¨ä¸­ã","å¯å¨è¶æ¶ï¼è¯·æ£æ¥æ¸¸ææ¯å¦å·²å¯å¨ã","æ­£å¨å¯å¨ {name}...","å·²å¤å¶æ¥å¿ã","ç± maj0r ç¨ {heart} å¶ä½","å¹é v{v}","éè¦ v{v}","æ æ¸¸æ","æ²¡æå¹éæç´¢çæ¸¸æã","è¿æ²¡æå¸¦ä½å¼çæ¸¸æãè¯·å°âæ¥æºâè·åã","ææ æ¥å¿ã","æ²¡æå¹éçè¡ã","æªå°±ç»ª","ææ æè¿è®°å½ãå¯å¨æ¸¸æææå¼å¶ä¿®æ¹å¨å³å¯ã","æ²¡æå¯å¤å¶çåå®¹ã","æ²¡æå·²å¼å¯çé¡¹ã","æå¼ä¿®æ¹å¨ï¼{name}","å·²æ¸é¤è¡¥ä¸èªå¨å è½½ã","å·²ä¿å­è¡¥ä¸èªå¨å è½½ã","äºç»´ç ä¸å¯ç¨ã","æ­£å¨ä» GitHub è¯»åæä»¶åè¡¨","ä»æ¶èä¸­ç§»é¤","éç½®å¤±è´¥ã","éç½®è®¾ç½®ï¼","å·²ä¿å­","æ­£å¨ä¿å­ä½å¼æä»¶","æ­£å¨ä¿å­...","ä¹åè¯·ä»çµèåéæ°ç CheatRunner.elfã","å·²éç½®è®¾ç½®ã","å³é­","å³é­ CheatRunnerï¼","æ¥æº {i}/{n}","è¯·åå¯å¨æ¸¸æã","æ¸¸æä»å¨å è½½ã","æ¸¸æä»å¨å¯å¨ï¼è¯·å ç§åéè¯ã","æ¸¸æåè¡¨ææ¶ä¸å¯ç¨ã","æ­¤ä½å¼é¡¹ææ¶æ æ³åæ¢ã","æ­¤ä½å¼é¡¹ä¹åå¯¼è´è¿æ¸¸æå´©æºãæ¸é¤æ è®°åå¯éè¯ã","æ­¤æä»¶å¯¹åºçæ¸¸æçæ¬ä¸å·²å®è£ççæ¬ä¸åã","è¿ä¼ä»ä¸»æºä¸­å é¤è¯¥æä»¶ãä½ å¯ä»¥å¨âæ¥æºâä¸­éæ°ä¸è½½ã","ç¨æ¶ {t}","å·²å¤å¶ç½åã","æ­£å¨è§£å {name}","ä¸æ¯æçè¡¥ä¸","ä¸ä¼ å¤±è´¥ã","è¯·ä½¿ç¨ .mc4ã.shn æ .json æä»¶ã","ä½¿ç¨å¶ä»çæ¬ï¼","ä»ç¶ä½¿ç¨","æ­£å¨ç­å¾ CheatRunner éæ°ä¸çº¿...","å·²å¤å¶ {label} å°å","æææ¥æº","å¹³å {s}","åå","ä½å¼æä»¶","å¹é v{v}","å·²å³é­ {n} ä¸ªä½å¼ã","å·²ä¿å­ {n} ä¸ªæ°å¢ææ´æ°çæä»¶","{n} ä¸ªæ¸¸æ","{n} ä¸ªæ¸¸æ","{n}å¤©å","{n}å°æ¶å","{n}åéå","{page} - CheatRunner","å·²å¼å¯ {a}/{n}","{h}å°æ¶{m}åé","{m}åé","{s}ç§","{m}å{s}ç§","ä½èï¼{a}","æ¬¢è¿ä½¿ç¨ CheatRunnerã","è¿æ²¡æä½å¼æä»¶ãå¯å¨âæ¥æºâè·åç¤¾åºä½å¼ï¼ææå¼æ¸¸æåæå¥æä»¶ã","åæ¶","ç»§ç»­","æ­£å¨éæ°è¿æ¥...","å·²å¤å¶","å·²å¤å¶ã","æªç¥","å¯¹è¯æ¡","å³","æé","æ­£å¸¸","ç¼ºå¤±","å¤±è´¥ã","å¯å¨ä¸­","éè¯","æ¸¸æ","æç´¢ä½å¼","å¨ X ä¸å³æ³¨ maj0r","å¨ Bluesky ä¸å³æ³¨ maj0r","ä½èï¼maj0r - ð§ð·","ç»é¡¹ç®ç¹ä¸ªæå§ï¼â­","ç»¯çº¢","èªå¨å è½½","éç½®","æ­¤æ¸¸ææ²¡æè¡¥ä¸ã","è¯·å°ä½ ç XML è¡¥ä¸æ¾å° /data/cheatrunner/patches/xml","æå¯ç¨æ´æ°","çæ¬ {v} å·²åå¸ã","å¨çº¿æµè§ä½å¼ç ","æç´¢ææå¸¦ä½å¼ç çæ¸¸æï¼æ¥çä½ çæ¸¸ææ¯å¦æã","æå¼ç½ç«","ç¤¾åºè¡¥ä¸ä»åº","éç¨äºä½ æ¸¸æçç°æè¡¥ä¸ãä¸ä¼ä¸è½½ä»»ä½åå®¹ï¼ä»åºå°å¨æ°æ ç­¾é¡µä¸­æå¼ã","æå¼ PS-Game-Patch","è§è§ææ","âç²¾ç®âä¼å³é­æ¨¡ç³ãé´å½±åå¨ç»ãè¿æ¯ PS5 æµè§å¨ä¸­çé»è®¤è®¾ç½®ã","å®æ´","ç²¾ç®","æ¾ç¤ºæ´å¤ï¼{n}ï¼","æ¨è","PS-Game-Patch å®æ¹åè¡çãè¡¥ä¸å°ä¿å­å° /data/cheatrunner/patchesã","ä¸è½½è¡¥ä¸","æ­£å¨ä¿å­è¡¥ä¸æä»¶","é¨åè¡¥ä¸åªæå¨æ¸¸æå¯å¨æ¶åºç¨æä¼çæï¼ä¿å­èªå¨å è½½å¹¶éæ°å¯å¨æ¸¸æã","æéæé¤","æ¥åé®é¢ï¼æå°ææè®¾ç½®æ¢å¤ä¸ºé»è®¤å¼ã","å¤å¶è¯æ­ä¿¡æ¯å¹¶æå¼ GitHub é®é¢é¡µé¢ã","ä¸ä¼æ¹å¨éææãè¡¥ä¸åèªå¨å è½½éç½®ã","ä¸åæ­£å¸¸ã","æéè¦æ³¨æçå°æ¹ï¼è¯·æ¥çä¸æ¹è¯¦æã","æ¥èªä½ çµèç .mc4ã.shn æ .json æä»¶ã","å¨âæä»¶âæâæ¥æºâä¸­æ·»å ","å¤è§","å¤è§ãä½å¼å¼æåææéé¡¹ãæ´æ¹ä¼å³æ¶ä¿å­ã","ä½å¼æä»¶","æ­¤æ¸¸æçä½å¼æä»¶","CheatRunner ä¼ååºè¿å°ä¸»æºä¸å·²å®è£çæ¸¸æãå®è£ä¸æ¬¾æ¸¸æåå·æ°åè¡¨ã","å¼å¯ä¹åæ æ³åºç¨ä½å¼åè¡¥ä¸ã","æ­£å¨æ£æ¥...","éæ©å¶ä»æä»¶","æ æ³å è½½","æ æ³å è½½ä½å¼æä»¶","æ æ³å è½½è¡¥ä¸","æ æ³å è½½è®¾ç½®","æ æ³å è½½ä½ çæ¸¸æ","æ æ³è¯»åä½å¼çå½åç¶æã","å¨âæ¥æºâä¸­ä¸è½½ç¤¾åºä½å¼ï¼æä»ä½ ççµèæ·»å æä»¶ã","å¨âæ¥æºâä¸­ä¸è½½ï¼æææä»¶æå°ä¸æ¹ã","ä¸ºä½ çæ¸¸æä¸è½½ç°æçä½å¼åè¡¥ä¸ãæµè§æ¶ä¸è½½ä¼ç»§ç»­è¿è¡ã","å¨âæ¥æºâä¸­ä¸è½½å®æ¹è¡¥ä¸åï¼ææä½ ç XML è¡¥ä¸æ¾å° /data/cheatrunner/patches/xmlã","æ­£å¨ä¸è½½","æ­£å¨ä¸è½½ {p}%","ä¸è½½","å¼æ","æ­£å¨è·éææ°æ¥å¿","è·åä½å¼","ç¶æ","æ¸¸æåº","ä½å¼å¼æååå­è¡¥ä¸çå®æ¶ç¶æï¼ä»¥å CheatRunner çè¿è¡è®°å½ã","æ°é»ãå¸®å©ï¼ä»¥å CheatRunner èåçäººã","æ­¤æ¸¸æè¿æ²¡æä½å¼æä»¶","æ²¡æä¸â{q}âå¹éçä½å¼ã","ææ æä»¶","ææªæ¾å°æ¸¸æ","æªæ¾å°æ¬å°æä»¶","æ­¤æ¸¸ææ²¡æè¡¥ä¸","æ­£å¨è¿è¡","æå¼è®¾ç½®","éè¯»æ¶å·²æå","åå¥½","âç²¾ç®âä¼ç§»é¤ææå¨ç»ãè¿æ¯ PS5 æµè§å¨ä¸­çé»è®¤è®¾ç½®ã","ä¿å­å¨ /data/cheatrunner/cheatsãæ è®°ä¸ºâä½¿ç¨ä¸­âçæä»¶å°±æ¯ä¿®æ¹å¨è¯»åçé£ä¸ªã","éå°é®é¢æ¶è¯·æäº¤æ¥åã","å¯å¨åå³å¯ä½¿ç¨ä½å¼","å¯å¨è¿æ¬¾æ¸¸æå³å¯ä½¿ç¨å®çä½å¼ã","ææ¯è¯¦æ","ä½å¼å¼æå·²å³é­ã","æ æ³è¯»åå½åå¼ï¼å æ­¤ä¸æ¾ç¤ºä»»ä½åå®¹ï¼ä»¥åæ¾ç¤ºéè¯¯çå¼ã","åºç¨ä½å¼åè¡¥ä¸æéçåºæ¬è®¾ç½®ã","æä»¶å­å¨ï¼ä½æ æ³è¯»åãå¯è½å·²æåï¼æä»å¨å¤å¶ä¸­ã","æä»¶å·²å è½½ï¼ä½åå®¹ä¸ºç©ºãè¯·ä¸ºè¿æ¬¾æ¸¸ææ¢ä¸ä¸ªæä»¶ã","æ¸¸æåè¡¨ææ¶ä¸å¯ç¨ãè¯·ç¡®è®¤ä¸»æºå¤äºå¼æºç¶æï¼ç¶åéè¯ã","æ æ³è¯»åè¿æ¬¾æ¸¸æçè¡¥ä¸æä»¶ãè¯·ç¨åéè¯ã","å¼å³ç¶æå¯è½å·²è¿æã","æ­¤é¢æ¿çä¸»é¢ãææåè¯­è¨ã","æ­¤ä½å¼æä»¶ä¸­æ²¡æä½å¼","è¯·å¨ä¸æ¹éè¯","è¯·å¨è®¾ç½®ä¸­å¼å¯","å±é©åºå","è·³å°ææ°","å é¤ä½¿ç¨ä¸­çæä»¶","ä½å¼æ¥æº","ç­éæ¸¸æ","æ¥çä½ çæ¸¸æ","ä½è","Download community patch sets. Patches are saved to /data/cheatrunner/patches.","Open repository","Patch source"]}};
var CR_FLAGS = {"ar-SA":"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 640 480\"><path fill=\"#006c35\" d=\"M0 0h640v480H0z\"/><path fill=\"#fff\" d=\"M170 190h300v30H170zM200 330h240v14H200z\"/></svg>","de-DE":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-de\" viewBox=\"0 0 640 480\"><path fill=\"#fc0\" d=\"M0 320h640v160H0z\"/><path fill=\"#000001\" d=\"M0 0h640v160H0z\"/><path fill=\"red\" d=\"M0 160h640v160H0z\"/></svg>","en-US":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-us\" viewBox=\"0 0 640 480\"><path fill=\"#bd3d44\" d=\"M0 0h640v480H0\"/><path stroke=\"#fff\" stroke-width=\"37\" d=\"M0 55.3h640M0 129h640M0 203h640M0 277h640M0 351h640M0 425h640\"/><path fill=\"#192f5d\" d=\"M0 0h364.8v258.5H0\"/><marker id=\"us-a\" markerHeight=\"30\" markerWidth=\"30\"><path fill=\"#fff\" d=\"m14 0 9 27L0 10h28L5 27z\"/></marker><path fill=\"none\" marker-mid=\"url(#us-a)\" d=\"m0 0 16 11h61 61 61 61 60L47 37h61 61 60 61L16 63h61 61 61 61 60L47 89h61 61 60 61L16 115h61 61 61 61 60L47 141h61 61 60 61L16 166h61 61 61 61 60L47 192h61 61 60 61L16 218h61 61 61 61 60z\"/></svg>","es-ES":"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 640 480\"><path fill=\"#c60b1e\" d=\"M0 0h640v480H0z\"/><path fill=\"#ffc400\" d=\"M0 120h640v240H0z\"/></svg>","fa-IR":"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 640 480\"><path fill=\"#239f40\" d=\"M0 0h640v160H0z\"/><path fill=\"#fff\" d=\"M0 160h640v160H0z\"/><path fill=\"#da0000\" d=\"M0 320h640v160H0z\"/><circle cx=\"320\" cy=\"240\" r=\"38\" fill=\"none\" stroke=\"#da0000\" stroke-width=\"14\"/></svg>","fr-FR":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-fr\" viewBox=\"0 0 640 480\"><path fill=\"#fff\" d=\"M0 0h640v480H0z\"/><path fill=\"#000091\" d=\"M0 0h213.3v480H0z\"/><path fill=\"#e1000f\" d=\"M426.7 0H640v480H426.7z\"/></svg>","it-IT":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-it\" viewBox=\"0 0 640 480\"><g fill-rule=\"evenodd\" stroke-width=\"1pt\"><path fill=\"#fff\" d=\"M0 0h640v480H0z\"/><path fill=\"#009246\" d=\"M0 0h213.3v480H0z\"/><path fill=\"#ce2b37\" d=\"M426.7 0H640v480H426.7z\"/></g></svg>","ja-JP":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-jp\" viewBox=\"0 0 640 480\"><defs><clipPath id=\"jp-a\"><path fill-opacity=\".7\" d=\"M-88 32h640v480H-88z\"/></clipPath></defs><g fill-rule=\"evenodd\" stroke-width=\"1pt\" clip-path=\"url(#jp-a)\" transform=\"translate(88 -32)\"><path fill=\"#fff\" d=\"M-128 32h720v480h-720z\"/><circle cx=\"523.1\" cy=\"344.1\" r=\"194.9\" fill=\"#bc002d\" transform=\"translate(-168.4 8.6)scale(.76554)\"/></g></svg>","ko-KR":"<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" id=\"flag-icons-kr\" viewBox=\"0 0 640 480\"><defs><clipPath id=\"kr-a\"><path fill-opacity=\".7\" d=\"M-95.8-.4h682.7v512H-95.8z\"/></clipPath></defs><g fill-rule=\"evenodd\" clip-path=\"url(#kr-a)\" transform=\"translate(89.8 .4)scale(.9375)\"><path fill=\"#fff\" d=\"M-95.8-.4H587v512H-95.8Z\"/><g transform=\"rotate(-56.3 361.6 -101.3)scale(10.66667)\"><g id=\"kr-c\"><path id=\"kr-b\" fill=\"#000001\" d=\"M-6-26H6v2H-6Zm0 3H6v2H-6Zm0 3H6v2H-6Z\"/><use xlink:href=\"#kr-b\" width=\"100%\" height=\"100%\" y=\"44\"/></g><path stroke=\"#fff\" d=\"M0 17v10\"/><path fill=\"#cd2e3a\" d=\"M0-12a12 12 0 0 1 0 24Z\"/><path fill=\"#0047a0\" d=\"M0-12a12 12 0 0 0 0 24A6 6 0 0 0 0 0Z\"/><circle cy=\"-6\" r=\"6\" fill=\"#cd2e3a\"/></g><g transform=\"rotate(-123.7 191.2 62.2)scale(10.66667)\"><use xlink:href=\"#kr-c\" width=\"100%\" height=\"100%\"/><path stroke=\"#fff\" d=\"M0-23.5v3M0 17v3.5m0 3v3\"/></g></g></svg>","pl-PL":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-pl\" viewBox=\"0 0 640 480\"><g fill-rule=\"evenodd\"><path fill=\"#fff\" d=\"M640 480H0V0h640z\"/><path fill=\"#dc143c\" d=\"M640 480H0V240h640z\"/></g></svg>","pt-BR":"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 640 480\"><path fill=\"#229e45\" d=\"M0 0h640v480H0z\"/><path fill=\"#f8e509\" d=\"m321 436 301-196L320 44 17 241z\"/><circle cx=\"320\" cy=\"240\" r=\"105\" fill=\"#2b49a3\"/><path fill=\"none\" stroke=\"#fff\" stroke-width=\"18\" d=\"M222 215c60-24 150-8 190 50\"/></svg>","ru-RU":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-ru\" viewBox=\"0 0 640 480\"><path fill=\"#fff\" d=\"M0 0h640v160H0z\"/><path fill=\"#0039a6\" d=\"M0 160h640v160H0z\"/><path fill=\"#d52b1e\" d=\"M0 320h640v160H0z\"/></svg>","th-TH":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-th\" viewBox=\"0 0 640 480\"><g fill-rule=\"evenodd\"><path fill=\"#f4f5f8\" d=\"M0 0h640v480H0z\"/><path fill=\"#2d2a4a\" d=\"M0 162.5h640v160H0z\"/><path fill=\"#a51931\" d=\"M0 0h640v82.5H0zm0 400h640v80H0z\"/></g></svg>","tr-TR":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-tr\" viewBox=\"0 0 640 480\"><g fill-rule=\"evenodd\"><path fill=\"#e30a17\" d=\"M0 0h640v480H0z\"/><path fill=\"#fff\" d=\"M407 247.5c0 66.2-54.6 119.9-122 119.9s-122-53.7-122-120 54.6-119.8 122-119.8 122 53.7 122 119.9\"/><path fill=\"#e30a17\" d=\"M413 247.5c0 53-43.6 95.9-97.5 95.9s-97.6-43-97.6-96 43.7-95.8 97.6-95.8 97.6 42.9 97.6 95.9z\"/><path fill=\"#fff\" d=\"m430.7 191.5-1 44.3-41.3 11.2 40.8 14.5-1 40.7 26.5-31.8 40.2 14-23.2-34.1 28.3-33.9-43.5 12-25.8-37z\"/></g></svg>","uk-UA":"<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"flag-icons-ua\" viewBox=\"0 0 640 480\"><g fill-rule=\"evenodd\" stroke-width=\"1pt\"><path fill=\"gold\" d=\"M0 0h640v480H0z\"/><path fill=\"#0057b8\" d=\"M0 0h640v240H0z\"/></g></svg>","zh-CN":"<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" id=\"flag-icons-cn\" viewBox=\"0 0 640 480\"><defs><path id=\"cn-a\" fill=\"#ff0\" d=\"M-.6.8 0-1 .6.8-1-.3h2z\"/></defs><path fill=\"#ee1c25\" d=\"M0 0h640v480H0z\"/><use xlink:href=\"#cn-a\" width=\"30\" height=\"20\" transform=\"matrix(71.9991 0 0 72 120 120)\"/><use xlink:href=\"#cn-a\" width=\"30\" height=\"20\" transform=\"matrix(-12.33562 -20.5871 20.58684 -12.33577 240.3 48)\"/><use xlink:href=\"#cn-a\" width=\"30\" height=\"20\" transform=\"matrix(-3.38573 -23.75998 23.75968 -3.38578 288 95.8)\"/><use xlink:href=\"#cn-a\" width=\"30\" height=\"20\" transform=\"matrix(6.5991 -23.0749 23.0746 6.59919 288 168)\"/><use xlink:href=\"#cn-a\" width=\"30\" height=\"20\" transform=\"matrix(14.9991 -18.73557 18.73533 14.99929 240 216)\"/></svg>"};
/* Translations. English text is the key; the page is translated in place and a language change reloads. */
var CRI = (function () {
  var LANGS = [
    ['en-US', 'English', 0], ['ar-SA', 'Ø§ÙØ¹Ø±Ø¨ÙØ©', 1], ['zh-CN', 'ç®ä½ä¸­æ', 0], ['fr-FR', 'FranÃ§ais', 0],
    ['de-DE', 'Deutsch', 0], ['it-IT', 'Italiano', 0], ['ja-JP', 'æ¥æ¬èª', 0], ['ko-KR', 'íêµ­ì´', 0],
    ['fa-IR', 'ÙØ§Ø±Ø³Û', 1], ['pl-PL', 'Polski', 0], ['pt-BR', 'PortuguÃªs (Brasil)', 0], ['ru-RU', 'Ð ÑÑÑÐºÐ¸Ð¹', 0],
    ['es-ES', 'EspaÃ±ol', 0], ['th-TH', 'à¹à¸à¸¢', 0], ['tr-TR', 'TÃ¼rkÃ§e', 0], ['uk-UA', 'Ð£ÐºÑÐ°ÑÐ½ÑÑÐºÐ°', 0]
  ];
  var STORE = 'cr_lang';
  var SKIP = 'script, style, textarea, .console, .mono, .tile-name, .t-info h1, .live-txt b, .sup-name';
  var WS = /^([\sÂ ]*)([\s\S]*?)([\sÂ ]*)$/;

  function find(id) { for (var i = 0; i < LANGS.length; i++) if (LANGS[i][0] === id) return LANGS[i]; return null; }
  function saved() { try { return localStorage.getItem(STORE) || ''; } catch (e) { return ''; } }
  function detect() {
    var n = String(navigator.language || 'en').toLowerCase(), base = n.split('-')[0];
    for (var i = 0; i < LANGS.length; i++) if (LANGS[i][0].toLowerCase() === n) return LANGS[i][0];
    for (var j = 0; j < LANGS.length; j++) if (LANGS[j][0].split('-')[0] === base) return LANGS[j][0];
    return 'en-US';
  }
  var lang = find(saved()) ? saved() : detect();
  var data = (typeof CR_I18N !== 'undefined' && CR_I18N.tr[lang]) || null;
  var map = null;
  if (data) {
    map = {};
    for (var i = 0; i < CR_I18N.keys.length; i++) if (data[i]) map[CR_I18N.keys[i]] = data[i];
  }

  function t(key, vars) {
    var s = (map && map[key]) || key;
    return vars ? s.replace(/\{(\w+)\}/g, function (m, k) { return vars[k] != null ? vars[k] : m; }) : s;
  }

  function tr(str) {
    var m = WS.exec(str), v = m[2] && map[m[2]];
    return v ? m[1] + v + m[3] : null;
  }
  function skipped(el) { return !!(el && el.closest && el.closest(SKIP)); }
  function textNodes(root, fn) {
    var w = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null, false), n;
    while ((n = w.nextNode())) fn(n);
  }
  function doText(n) {
    if (n.__t === n.nodeValue || skipped(n.parentNode)) return;
    var v = tr(n.nodeValue);
    if (v !== null && v !== n.nodeValue) { n.nodeValue = v; n.__t = v; }
  }
  var ATTRS = ['title', 'aria-label', 'placeholder'];
  function doAttrs(el) {
    if (skipped(el)) return;
    for (var i = 0; i < ATTRS.length; i++) {
      var cur = el.getAttribute(ATTRS[i]);
      if (cur) { var v = tr(cur); if (v !== null && v !== cur) el.setAttribute(ATTRS[i], v); }
    }
  }
  function translate(root) {
    if (!map) return;
    if (root.nodeType === 3) { doText(root); return; }
    if (root.nodeType !== 1) return;
    textNodes(root, doText);
    if (root.hasAttribute && (root.hasAttribute('title') || root.hasAttribute('aria-label') || root.hasAttribute('placeholder'))) doAttrs(root);
    var els = root.querySelectorAll('[title], [aria-label], [placeholder]');
    for (var i = 0; i < els.length; i++) doAttrs(els[i]);
  }

  var obs = null;

  function start() {
    var root = document.documentElement;
    root.lang = lang;
    root.dir = find(lang)[2] ? 'rtl' : 'ltr';
    if (map) {
      translate(document.body);
      document.title = tr(document.title) || document.title;
      if (window.MutationObserver) {
        // Runs as a microtask, so changes are translated before the next paint.
        obs = new MutationObserver(function (recs) {
          for (var i = 0; i < recs.length; i++) {
            var r = recs[i];
            if (r.type === 'childList') { for (var j = 0; j < r.addedNodes.length; j++) translate(r.addedNodes[j]); }
            else translate(r.target);
          }
          obs.takeRecords();
        });
        obs.observe(document.body, { childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ATTRS });
      }
    }
    root.classList.remove('i18n-wait');
  }

  function flag(id) {
    var svg = typeof CR_FLAGS !== 'undefined' && CR_FLAGS[id];
    return svg ? 'data:image/svg+xml,' + encodeURIComponent(svg) : '';
  }
  function pick(id) {
    if (!find(id)) return;
    try { localStorage.setItem(STORE, id); } catch (e) {}
    location.reload();
  }

  return { t: t, start: start, langs: LANGS, lang: lang, flag: flag, pick: pick };
})();
(function () {
  'use strict';

  var t = CRI.t;
  var isPS5 = /PlayStation/i.test(navigator.userAgent);
  if (isPS5) document.documentElement.classList.add('ps5');
  var rootCls = document.documentElement.classList;

  /* ---------------------------------------------------------------- helpers */
  var $ = function (s, r) { return (r || document).querySelector(s); };
  var view = $('#view');
  var sleep = function (ms) { return new Promise(function (r) { setTimeout(r, ms); }); };
  function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }
  function ic(name, cls) {
    return '<svg class="ic' + (cls ? ' ' + cls : '') + '"><use href="#i-' + name + '" xlink:href="#i-' + name + '"/></svg>';
  }
  function sect(icon, title, desc, side, tone) {
    return '<div class="sec-head"><div class="sec-txt"><span class="sec-ic' + (tone ? ' ' + tone : '') + '">' + ic(icon) + '</span><div><h2>' + title + '</h2>'
      + (desc ? '<p>' + desc + '</p>' : '') + '</div></div>' + (side ? '<div class="sec-side">' + side + '</div>' : '') + '</div>';
  }
  function stage(eyebrow, title, sub, acts) {
    return '<div class="stage"><div class="stage-txt"><div class="eyebrow">' + eyebrow + '</div><h1>' + title + '</h1>' + (sub ? '<p class="stage-sub">' + sub + '</p>' : '') + '</div>'
      + (acts ? '<div class="stage-act">' + acts + '</div>' : '') + '</div>';
  }
  function stateBox(kind, icon, title, text, acts, tech) {
    return '<div class="state ' + kind + '"><span class="state-ic">' + ic(icon) + '</span><h3>' + title + '</h3>' + (text ? '<p>' + text + '</p>' : '')
      + (acts ? '<div class="row">' + acts + '</div>' : '')
      + (tech ? '<details class="tech"><summary>' + t('Technical details') + '</summary><pre>' + esc(tech) + '</pre></details>' : '') + '</div>';
  }
  var errText = function (e) { return e ? [e.status ? 'HTTP ' + e.status : '', e.code || '', e.message || ''].filter(Boolean).join(' - ') : ''; };
  function store(k, v) {
    try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v); } catch (e) {}
    return null;
  }
  var cleanId = function (v) { return String(v || '').trim().toUpperCase(); };
  var isTitleId = function (id) { return /^(PPSA|CUSA|ULUS|ULES|ULJS|ULKS|SLUS|SCUS|SLES|SCES|SLPS|SLPM|SCED|SLED|SCPS)[0-9A-Z]{5}$/.test(String(id || '').toUpperCase()); };

  /* ------------------------------------------------------------------- api */
  var sema = { n: 2, q: [] };
  function acquire() { return new Promise(function (res) { if (sema.n > 0) { sema.n--; res(); } else sema.q.push(res); }); }
  function release() { if (sema.q.length) sema.q.shift()(); else sema.n++; }

  async function call(url, opts, method, body) {
    opts = typeof opts === 'number' ? { timeoutMs: opts } : (opts || {});
    var ms = opts.timeoutMs || (method === 'POST' ? 15000 : 10000);
    var retries = typeof opts.retries === 'number' ? opts.retries : 2;
    await acquire();
    try {
      for (var attempt = 0; ; attempt++) {
        if (attempt > 0) await sleep(attempt * 500);
        var ctrl = new AbortController();
        var to = setTimeout(function () { ctrl.abort(); }, ms);
        try {
          var init = { method: method, cache: 'no-store', signal: ctrl.signal };
          if (body !== undefined) {
            init.headers = { 'Content-Type': 'application/json' };
            init.body = JSON.stringify(body);
          }
          var res = await fetch(url, init);
          var txt = await res.text();
          var js = null;
          try { js = JSON.parse(txt); } catch (e) {}
          if (!res.ok) {
            var err = new Error(js && (js.message || js.error) ? (js.message || js.error) : 'HTTP ' + res.status);
            err.status = res.status; err.body = js; err.code = js && js.error ? String(js.error) : '';
            if (res.status === 503 && err.code === 'too_many_requests' && attempt < retries) continue;
            throw err;
          }
          return js || { ok: true };
        } finally { clearTimeout(to); }
      }
    } finally { release(); }
  }
  var get = function (u, o) { return call(u, o, 'GET'); };
  var post = function (u, b, o) { return call(u, o, 'POST', b || {}); };

  /* ----------------------------------------------------------- toast/modal */
  function toast(msg, level) {
    var box = $('#toasts'), lv = level || 'info';
    msg = String(msg || '');
    var last = box.lastChild;
    if (last && last.getAttribute('data-m') === msg) return;
    var el = document.createElement('div');
    el.className = 'toast ' + lv;
    el.setAttribute('data-m', msg);
    el.innerHTML = ic(lv === 'success' ? 'check' : lv === 'info' ? 'bolt' : 'warn') + '<span></span>';
    el.lastChild.textContent = msg;
    box.appendChild(el);
    void el.offsetWidth;
    el.classList.add('in');
    while (box.children.length > 3) box.removeChild(box.firstChild);
    setTimeout(function () {
      el.classList.remove('in');
      setTimeout(function () { if (el.parentNode) el.parentNode.removeChild(el); }, 240);
    }, lv === 'error' ? 5200 : 3200);
  }
  var modalDone = null, modalFrom = null;
  function openModal(html, onClose, cls) {
    var card = $('#modalCard'), m = $('#modal');
    modalFrom = document.activeElement;
    card.className = 'modal-card' + (cls ? ' ' + cls : '');
    card.removeAttribute('aria-describedby');
    card.innerHTML = html;
    var h = card.querySelector('h2');
    if (h) { h.id = 'mTitle'; card.setAttribute('aria-labelledby', 'mTitle'); }
    else { card.removeAttribute('aria-labelledby'); card.setAttribute('aria-label', cls === 'palette' ? 'Search' : 'Dialog'); }
    m.classList.toggle('top', cls === 'palette');
    modalDone = onClose || null;
    m.hidden = false;
    void m.offsetWidth;
    m.classList.add('show');
    var f = card.querySelector(card.querySelector('.btn.danger') ? '.btn.ghost' : '.btn:not(.ghost), button, input');
    if (f) f.focus();
  }
  function closeModal(result) {
    var m = $('#modal');
    m.classList.remove('show');
    setTimeout(function () { if (!m.classList.contains('show')) m.hidden = true; }, 180);
    var cb = modalDone; modalDone = null;
    if (modalFrom && modalFrom.focus) { try { modalFrom.focus(); } catch (e) {} }
    modalFrom = null;
    if (cb) cb(result);
  }
  function confirmBox(title, text, okLabel, danger) {
    return new Promise(function (resolve) {
      openModal('<span class="dlg-ic' + (danger ? ' danger' : '') + '">' + ic(danger ? 'warn' : 'info') + '</span><h2>' + esc(title) + '</h2><p id="mDesc">' + esc(text) + '</p><div class="modal-actions">'
        + '<button class="btn ghost" data-act="modalNo">' + t('Cancel') + '</button>'
        + '<button class="btn' + (danger ? ' danger' : '') + '" data-act="modalYes">' + esc(okLabel || t('Continue')) + '</button></div>',
        function (r) { resolve(!!r); });
      var c = $('#modalCard'); if ($('#mDesc')) c.setAttribute('aria-describedby', 'mDesc');
    });
  }
  function flash(el, label) {
    if (!el || el.__flash) return;
    el.__flash = el.innerHTML;
    el.classList.add('done');
    el.innerHTML = ic('check', 'sm') + esc(label);
    setTimeout(function () { el.innerHTML = el.__flash; el.classList.remove('done'); el.__flash = null; }, 1500);
  }
  async function copyText(text, okMsg, btn) {
    text = String(text || '');
    if (!text.trim()) { toast('Nothing to copy.', 'warn'); return; }
    var done = function () { if (btn) flash(btn, 'Copied'); else toast(okMsg || 'Copied.', 'success'); };
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) { await navigator.clipboard.writeText(text); done(); return; }
    } catch (e) {}
    try {
      var ta = document.createElement('textarea');
      ta.value = text; ta.style.cssText = 'position:fixed;left:-9999px;top:0';
      document.body.appendChild(ta); ta.select();
      var ok = document.execCommand('copy');
      document.body.removeChild(ta);
      if (ok) { done(); return; }
    } catch (e) {}
    toast('Copy failed. Select the text manually.', 'error');
  }
  // Lowercases and strips accents so "pokemon" finds "PokÃ©mon".
  function fold(s) {
    var n = String(s == null ? '' : s);
    try { n = n.normalize('NFD').replace(/[Ì-Í¯]/g, ''); } catch (e) {}
    return n.toLowerCase();
  }
  function skTiles(n) {
    var h = '';
    for (var i = 0; i < n; i++) h += '<div class="sk-tile"><div class="sk"></div><div class="sk sk-l"></div><div class="sk sk-l"></div></div>';
    return h;
  }
  function skRows(n) {
    var h = '';
    for (var i = 0; i < n; i++) h += '<div class="sk sk-row"></div>';
    return h;
  }

  /* ----------------------------------------------------------------- state */
  var S = {
    games: [], gamesLoaded: false, activity: {}, fav: new Set(), recent: [],
    filter: 'ALL', sort: 'name_asc', q: '',
    running: '', launching: '', launchBusy: false,
    state: null, cfg: {}, names: {}, lookupQ: [], lookupDone: {}, lookupBusy: false,
    dl: null, g: null
  };
  function newGame(id) {
    return { id: id, tab: 'cheats', doc: null, noFile: false, err: null, stateErr: null, rows: {}, dbg: null, filter: '', applying: {},
      auto: [], patch: null, patchAuto: [], loaded: false, patchLoaded: false };
  }

  var gid = function (g) { return g.titleId || g.title_id || ''; };
  function gname(g) { var id = gid(g); return S.names[id] || g.titleName || g.title_name || id || 'Unknown'; }
  function gplat(g) { var id = gid(g); return g.platform || (id.indexOf('PPSA') === 0 ? 'PS5' : id.indexOf('CUSA') === 0 ? 'PS4' : 'APP'); }
  function gicon(g) { return (g.iconUrl || '/appdb/icon?id=' + encodeURIComponent(gid(g))) + '&t=1'; }
  function findGame(id) { for (var i = 0; i < S.games.length; i++) if (gid(S.games[i]) === id) return S.games[i]; return null; }
  function isApp(g) { return g.kind === 'app' || g.isApp === true || gplat(g) === 'APP'; }
  function unresolvedName(id, n) {
    n = String(n || '').trim();
    return !n || n.toUpperCase() === id || /^(CUSA|PPSA|NPXS|ULUS|ULES|SLUS|SCUS|SLES|SCES)[-_]?[0-9A-Z]{5}$/i.test(n) || /^unknown/i.test(n);
  }
  function fmtPlay(s) {
    if (!s || s < 60) return '\u2014';
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
    return h ? t('{h}h {m}m', { h: h, m: m }) : t('{m}m', { m: m });
  }
  function fmtVer(v) {
    v = String(v || '').replace(/^v/i, '').trim();
    if (!v || v === 'unknown') return '\u2014';
    return v.replace(/^0+(?=\d)/, '');
  }
  function platBadge(g) {
    var p = gplat(g);
    return (p === 'PS5' || p === 'PS4') ? '<span class="plat-badge ' + p + '">' + p + '</span>' : '';
  }

  /* ------------------------------------------------------ favorites/recents */
  async function loadFavs() {
    try {
      var d = await get('/api/favorites', { retries: 0 });
      S.fav = new Set(Array.isArray(d.favorites) ? d.favorites : []);
      S.recent = Array.isArray(d.recents) ? d.recents : [];
    } catch (e) {}
  }
  function toggleFav(id) {
    var on = !S.fav.has(id);
    if (on) S.fav.add(id); else S.fav.delete(id);
    post('/api/favorites/set?id=' + encodeURIComponent(id) + '&fav=' + (on ? 1 : 0)).catch(function () {});
  }
  function pushRecent(id) {
    S.recent = [id].concat(S.recent.filter(function (x) { return x !== id; })).slice(0, 12);
    post('/api/favorites/recent?id=' + encodeURIComponent(id)).catch(function () {});
  }

  /* ----------------------------------------------------------------- games */
  async function loadGames(force) {
    if (S.loadingGames) return S.loadingGames;
    S.loadingGames = (async function () {
      try {
        var d = null;
        for (var i = 0; i < 4; i++) {
          try { d = await get(force ? '/api/games?refresh=1' : '/api/games', { timeoutMs: 15000 }); break; }
          catch (e) { if (i >= 3) throw e; await sleep((i + 1) * 1000); }
        }
        S.games = (d.games || d.titles || []).filter(function (g) { return isTitleId(gid(g)); }).map(function (g) {
          g.titleId = gid(g); g.running = !!g.running; return g;
        });
        S.gamesLoaded = true; S.gamesError = false;
        S.games.forEach(queueLookup);
        get('/api/activity', { retries: 0 }).then(function (a) {
          S.activity = {};
          (a.titles || []).forEach(function (t) { if (t.titleId) S.activity[t.titleId] = t; });
          if (route().page === 'games') paintGrid();
        }).catch(function () {});
      } catch (e) {
        S.gamesError = true;
        toast('Could not load the game list.', 'error');
      } finally { S.loadingGames = null; }
      var pg = route().page;
      if (pg === 'games') paintGrid(); else if (pg === 'game') paintHero();
    })();
    return S.loadingGames;
  }
  function queueLookup(g) {
    var id = gid(g);
    if (S.lookupDone[id] || !unresolvedName(id, gname(g))) return;
    S.lookupDone[id] = 1; S.lookupQ.push(id); pumpLookup();
  }
  async function pumpLookup() {
    if (S.lookupBusy) return;
    S.lookupBusy = true;
    while (S.lookupQ.length) {
      var id = S.lookupQ.shift();
      try {
        var r = await get('/api/titles/lookup?titleId=' + encodeURIComponent(id), { timeoutMs: 12000, retries: 0 });
        if (r && r.ok && r.name && !unresolvedName(id, r.name)) {
          S.names[id] = String(r.name);
          paintStatusChip();
          var p = route().page;
          if (p === 'games') paintGrid(); else if (p === 'game') paintHero();
        }
      } catch (e) {}
    }
    S.lookupBusy = false;
  }

  function inFilter(g, f) {
    var id = gid(g), app = isApp(g);
    if (f === 'FAV') return S.fav.has(id);
    if (f === 'RECENT') return S.recent.indexOf(id) >= 0;
    if (f === 'CHEATS') return !!g.hasCheat;
    if (f === 'APP') return app;
    if (f === 'ALL') return !app;
    return !app && gplat(g) === f;
  }
  // The folded name is cached on the game, since sorting and searching call it many times.
  function nkey(g) {
    var n = gname(g);
    if (g.__n !== n) { g.__n = n; g.__f = fold(n); }
    return g.__f;
  }
  function filtered() {
    var q = fold(S.q.trim());
    var list = S.games.filter(function (g) {
      if (!inFilter(g, S.filter)) return false;
      return !q || nkey(g).indexOf(q) >= 0 || gid(g).toLowerCase().indexOf(q) >= 0;
    });
    list.sort(function (a, b) {
      if (S.filter === 'RECENT') return S.recent.indexOf(gid(a)) - S.recent.indexOf(gid(b));
      var ar = gid(a) === S.running ? 0 : 1, br = gid(b) === S.running ? 0 : 1;
      if (ar !== br) return ar - br;
      var af = S.fav.has(gid(a)) ? 0 : 1, bf = S.fav.has(gid(b)) ? 0 : 1;
      if (af !== bf) return af - bf;
      if (S.sort === 'id_asc') return gid(a).localeCompare(gid(b));
      if (S.sort === 'cheats_first') { var ac = a.hasCheat ? 0 : 1, bc = b.hasCheat ? 0 : 1; if (ac !== bc) return ac - bc; }
      var an = nkey(a), bn = nkey(b);
      return S.sort === 'name_desc' ? bn.localeCompare(an) : an.localeCompare(bn);
    });
    return list;
  }

  var IMG_ATTR = ' onload="this.classList.add(\'ld\')" onerror="crImgErr(this)"';
  /* Cover requests can be refused when the console has too many open connections. */
  window.crImgErr = function (img) {
    var n = +img.getAttribute('data-try') || 0;
    if (n >= 4) { img.style.display = 'none'; return; }
    img.setAttribute('data-try', n + 1);
    var src = img.getAttribute('data-src') || img.getAttribute('src');
    img.setAttribute('data-src', src);
    setTimeout(function () { img.src = src + (src.indexOf('?') < 0 ? '?' : '&') + 'r=' + (n + 1); }, 600 * (n + 1) + Math.random() * 400);
  };
  function tileHtml(g, i) {
    var id = gid(g), live = id === S.running, fav = S.fav.has(id), name = gname(g);
    var tags = '';
    if (g.hasCheat) tags += '<span class="pill on">' + ic('cheat', 'sm') + 'Cheats</span>';
    if (g.hasPatch) tags += '<span class="pill patch">' + ic('patch', 'sm') + 'Patches</span>';
    if (g.autoloadCheats || g.autoloadPatches) tags += '<span class="pill ok">' + ic('save', 'sm') + 'Autoload</span>';
    var secs = g.playTime || (S.activity[id] ? S.activity[id].totalSeconds : 0);
    var canTrain = !isApp(g) || g.hasCheat;
    var play = '<button class="btn' + (live && canTrain ? ' ghost' : '') + '" data-act="launch" data-id="' + esc(id) + '"' + (S.launchBusy ? ' disabled' : '') + '>'
      + ic('play', 'fill') + (S.launching === id ? 'Launching' : 'Play') + '</button>';
    var train = canTrain ? '<button class="btn' + (live ? '' : ' ghost') + '" data-act="open" data-id="' + esc(id) + '">' + ic('bolt') + 'Trainer</button>' : '';
    return '<article class="tile' + (live ? ' running' : '') + '" data-act="open" data-id="' + esc(id) + '" tabindex="0" style="--i:' + Math.min(i || 0, 14) + '" aria-label="' + esc(name) + '">'
      + '<div class="tile-cover"><span class="ph" aria-hidden="true">' + esc(name.charAt(0).toUpperCase()) + '</span>'
      + '<div class="tile-img" style="background-image:url(' + esc(gicon(g)) + ')"></div>'
      + '<button class="icon-btn tile-fav' + (fav ? ' fav-on' : '') + '" data-act="fav" data-id="' + esc(id) + '" aria-label="' + (fav ? 'Remove from favorites' : 'Add to favorites') + '" aria-pressed="' + fav + '">' + ic('star', fav ? 'fill' : '') + '</button>'
      + (live ? '<span class="pill tile-live"><i class="dot live"></i>Running</span>' : '') + '</div>'
      + '<div class="tile-body"><div class="tile-name">' + esc(name) + '</div>'
      + '<div class="tile-id mono">' + esc(id) + '</div>'
      + '<div class="tile-tags">' + platBadge(g) + tags + '</div>'
      + '<div class="tile-stats"><div class="tile-stat"><span>Version</span><b>' + esc(fmtVer(g.version)) + '</b></div>'
      + '<div class="tile-stat"><span>Time played</span><b>' + fmtPlay(secs) + '</b></div>'
      + '<div class="tile-stat"><span>Last played</span><b>' + (g.lastAccessTime ? ago(g.lastAccessTime) : '\u2014') + '</b></div></div>'
      + '<div class="tile-actions">' + play + train + '</div></div></article>';
  }

  var CHIPS = [['ALL', 'All'], ['FAV', 'Favorites'], ['RECENT', 'Recent'], ['CHEATS', 'With cheats'], ['PS5', 'PS5'], ['PS4', 'PS4'], ['APP', 'Apps']];
  function paintChips() {
    var el = $('#chips'); if (!el) return;
    el.innerHTML = CHIPS.map(function (c) {
      var n = S.gamesLoaded ? S.games.filter(function (g) { return inFilter(g, c[0]); }).length : '';
      return '<button class="chip' + (S.filter === c[0] ? ' active' : '') + '" data-act="chip" data-f="' + c[0] + '" aria-pressed="' + (S.filter === c[0]) + '">' + c[1] + (n !== '' ? '<em>' + n + '</em>' : '') + '</button>';
    }).join('');
  }
  function paintLive() {
    var el = $('#liveBar'); if (!el) return;
    var g = S.running ? findGame(S.running) : null;
    if (!g) { el.innerHTML = ''; return; }
    var id = gid(g), ver = fmtVer(g.version);
    el.innerHTML = '<div class="playing" data-act="open" data-id="' + esc(id) + '" role="button" tabindex="0" aria-label="' + esc(gname(g)) + '"><div class="live-bg" style="background-image:url(' + esc(gicon(g)) + ')"></div>'
      + '<div class="cover live-cover"><span class="ph" aria-hidden="true">' + esc(gname(g).charAt(0).toUpperCase()) + '</span><div class="tile-img" style="background-image:url(' + esc(gicon(g)) + ')"></div></div>'
      + '<div class="live-txt"><div class="live-kicker"><i class="dot live"></i>Now playing</div><b>' + esc(gname(g)) + '</b>'
      + '<div class="live-meta">' + (platBadge(g) ? '<span style="margin:2px 8px 0 0">' + platBadge(g) + '</span>' : '') + (ver !== '\u2014' ? '<span class="pill">' + esc(ver) + '</span>' : '')
      + (g.hasCheat ? '<span class="pill on">' + ic('cheat', 'sm') + 'Cheats</span>' : '<span class="pill warn">' + ic('warn', 'sm') + 'No cheat file yet</span>') + '</div></div>'
      + '<button class="btn lg" data-act="open" data-id="' + esc(id) + '">' + ic('bolt') + 'Open trainer</button></div>';
  }

  function paintGrid() {
    var grid = $('#grid'); if (!grid) return;
    paintChips(); paintLive();
    var clr = $('#gclear'); if (clr) clr.hidden = !S.q;
    var list = filtered();
    var cnt = $('#gamesCount'); if (cnt) cnt.textContent = S.gamesLoaded ? t(list.length === 1 ? '{n} title' : '{n} titles', { n: list.length }) : '';
    if (!S.gamesLoaded && !S.gamesError) { grid.innerHTML = skTiles(10); return; }
    if (!list.length) {
      var filt = S.q || S.filter !== 'ALL';
      var box;
      if (S.gamesError && !S.games.length) box = stateBox('err', 'warn', 'Could not load your games', 'The game list is unavailable right now. Check that the console is awake, then try again.', '<button class="btn" data-act="refreshGames">' + ic('refresh') + 'Try again</button>');
      else if (!S.games.length) box = stateBox('info', 'games', 'No games found yet', 'CheatRunner lists the games installed on this console. Install one, then refresh the list.', '<button class="btn" data-act="refreshGames">' + ic('refresh') + 'Refresh</button>');
      else {
        var msg = S.filter === 'FAV' && !S.q ? 'No favorites yet. Tap the star on a game to add it.'
          : S.filter === 'RECENT' && !S.q ? 'Nothing recent. Launch a game or open its trainer.'
          : S.filter === 'CHEATS' && !S.q ? 'No games with cheats yet. Get some from Sources.'
          : 'No games match your search.';
        box = stateBox('', 'search', msg, '', filt ? '<button class="btn ghost" data-act="clearFilters">Clear filters</button>' : '');
      }
      grid.innerHTML = '<div style="grid-column:1/-1">' + box + '</div>';
      return;
    }
    // Stagger the tiles in only when the filter or sort changed, never on a poll repaint.
    var key = S.filter + '|' + S.sort;
    if (S.gridKey !== key) {
      S.gridKey = key; grid.classList.remove('enter'); void grid.offsetWidth; grid.classList.add('enter');
      clearTimeout(S.gridTimer); S.gridTimer = setTimeout(function () { grid.classList.remove('enter'); }, 900);
    }
    var lk = key + '|' + S.q;
    if (S.listKey !== lk) { S.listKey = lk; S.shown = GRID_CHUNK; }
    S.gridList = list;
    var shown = Math.min(S.shown, list.length);
    var html = list.slice(0, shown).map(tileHtml).join('') + (shown < list.length ? moreBar(list.length - shown) : '');
    if (grid.__h !== html) {
      grid.__h = html;
      withFocus(grid, function () { grid.innerHTML = html; });
      padStart(grid.querySelector('.tile'));
    }
  }
  var GRID_CHUNK = isPS5 ? 12 : 36;
  function moreBar(left) {
    return '<div class="grid-more" id="gridMore"><button class="btn ghost" data-act="gridMore">' + esc(t('Show more ({n})', { n: left })) + '</button></div>';
  }
  function growGrid() {
    var grid = $('#grid'), more = $('#gridMore'), list = S.gridList;
    if (!grid || !more || !list) return;
    var from = Math.min(S.shown, list.length);
    S.shown = Math.min(list.length, from + GRID_CHUNK);
    more.parentNode.removeChild(more);
    grid.insertAdjacentHTML('beforeend', list.slice(from, S.shown).map(tileHtml).join('') + (S.shown < list.length ? moreBar(list.length - S.shown) : ''));
    grid.__h = list.slice(0, S.shown).map(tileHtml).join('') + (S.shown < list.length ? moreBar(list.length - S.shown) : '');
  }
  var scrollTick = false;
  window.addEventListener('scroll', function () {
    if (scrollTick) return;
    scrollTick = true;
    requestAnimationFrame(function () {
      scrollTick = false;
      var m = $('#gridMore');
      if (m && m.getBoundingClientRect().top < window.innerHeight + 900) growGrid();
    });
  }, { passive: true });

  function viewGames() {
    S.gridKey = '';
    var welcome = '';
    if (S.gamesLoaded && S.games.length && !S.games.some(function (g) { return g.hasCheat; }) && !store('cr_welcome')) {
      welcome = '<div class="banner info">' + ic('bolt') + '<div class="txt"><b>Welcome to CheatRunner.</b> No cheat files yet. Grab community cheats from Sources, or open a game and drop a file in.</div>'
        + '<a class="btn sm" href="#/sources">Get cheats</a><button class="icon-btn" data-act="dismissWelcome" aria-label="Dismiss">' + ic('x') + '</button></div>';
    }
    view.innerHTML = stage('Library', 'Games', '<span id="gamesCount"></span>') + welcome + '<div id="liveBar"></div>'
      + '<div class="games-bar"><div class="search">' + ic('search') + '<input class="input" id="gq" data-in="q" placeholder="Search by name or title ID" aria-label="Search games" value="' + esc(S.q) + '" autocomplete="off">'
      + '<button class="icon-btn search-clear" id="gclear" data-act="clearQ" aria-label="Clear search" hidden>' + ic('x', 'sm') + '</button></div>'
      + '<select class="select" data-change="sort" aria-label="Sort games">'
      + [['name_asc', 'Name A-Z'], ['name_desc', 'Name Z-A'], ['id_asc', 'Title ID'], ['cheats_first', 'Cheats first']].map(function (o) { return '<option value="' + o[0] + '"' + (S.sort === o[0] ? ' selected' : '') + '>' + o[1] + '</option>'; }).join('')
      + '</select><button class="btn ghost" id="gRefresh" data-act="refreshGames">' + ic('refresh') + 'Refresh</button></div>'
      + '<div class="chips-bar"><div class="chips" id="chips" role="group" aria-label="Filter games"></div></div><div class="grid" id="grid"></div>';
    paintGrid();
  }

  /* ---------------------------------------------------------------- launch */
  async function launch(id) {
    if (!id || S.launchBusy) return;
    pushRecent(id);
    S.launchBusy = true; S.launching = id; paintGrid();
    try {
      var r = await get('/launch?titleId=' + encodeURIComponent(id));
      if (!(r && r.ok)) { toast('Launch failed.', 'error'); return; }
      toast(t('Launching {name}...', { name: gname(findGame(id) || { titleId: id }) }), 'success');
      var t0 = Date.now(), fails = 0;
      while (Date.now() - t0 < 40000) {
        var ls = null;
        try { ls = await get('/api/launch/status', { timeoutMs: 5000 }); fails = 0; }
        catch (e) { if (++fails >= 3) { toast('CheatRunner stopped responding during launch.', 'warn'); break; } await sleep(800); continue; }
        if (!ls.busy) {
          if (ls.phase === 'timeout' || ls.stale) toast('Launch timed out. Check if the game started.', 'warn');
          else if (ls.phase === 'ready' || ls.verified) toast(ls.message || 'Game launched.', 'success');
          else toast(t('Launch failed: {msg}', { msg: ls.message || id }), 'error');
          break;
        }
        await sleep(800);
      }
      await refreshStatus(); await loadGames(true);
    } catch (e) {
      toast('Launch request failed. The game may still be starting.', 'warn');
    } finally {
      S.launchBusy = false; S.launching = '';
      if (route().page === 'games') paintGrid();
    }
  }

  /* ------------------------------------------------------------ game page */
  var STATE_ON = { on: 1, on_unverified: 1, mixed: 1 };

  function rowMap(mods) {
    var m = {};
    (mods || []).forEach(function (r) {
      var i = Number(r.index); if (!isFinite(i)) return;
      m[i] = {
        state: String(r.state || 'unknown').toLowerCase(), reason: String(r.reason || ''),
        visual: String(r.visualState || '').toLowerCase(), canToggle: !!r.canToggle,
        nextOn: r.nextOn != null ? !!r.nextOn : null, block: String(r.blockReason || '')
      };
    });
    return m;
  }
  async function loadCheatState(id) {
    try {
      var st = await get('/api/cheats/state?titleId=' + encodeURIComponent(id) + '&debug=1');
      if (!S.g || S.g.id !== id) return;
      S.g.dbg = st; S.g.rows = rowMap(st.mods); S.g.stateErr = null;
    } catch (e) {
      // A missing file has its own state; any other failure means the live state could not be read.
      if (S.g && S.g.id === id && !(e.status === 404 && e.code === 'no cheat file')) S.g.stateErr = e;
    }
  }
  async function loadGamePage(id) {
    var g = S.g; g.err = null; g.noFile = false;
    try { g.doc = await get('/api/cheats?titleId=' + encodeURIComponent(id), { timeoutMs: 15000 }); }
    catch (e) {
      g.doc = null;
      if (e.status === 404 && e.code === 'no cheat file') g.noFile = true; else g.err = e;
    }
    if (S.g !== g) return;
    await loadCheatState(id);
    try { var a = await get('/api/cheats/profile?titleId=' + encodeURIComponent(id), { retries: 0 }); g.auto = Array.isArray(a.mods) ? a.mods : []; }
    catch (e) { g.auto = []; }
    g.loaded = true;
    if (S.g === g && route().page === 'game') { paintHero(); paintBody(); }
  }

  // What the trainer needs, one cell each, so a missing piece is visible at a glance.
  function readyHtml(g) {
    var game = findGame(g.id) || {}, live = S.running === g.id, d = g.dbg || {};
    function rd(cls, icon, label, val, sub) {
      return '<div class="rd ' + cls + '"><span class="rd-ic">' + ic(icon) + '</span><div class="rd-txt"><small>' + label + '</small><b>' + val + '</b>' + (sub ? '<span>' + sub + '</span>' : '') + '</div></div>';
    }
    var gameRd = live ? rd('ok', 'play', 'Game', 'Running', esc(gplat(game))) : rd('warn', 'power', 'Game', 'Not running', 'Start it to use cheats');
    var fileRd;
    if (!g.loaded) fileRd = rd('', 'file', 'Cheat file', 'Checking...', '');
    else if (g.err) fileRd = rd('err', 'warn', 'Cheat file', 'Could not load', 'Try again below');
    else if (g.noFile) fileRd = rd('warn', 'file', 'Cheat file', 'No file yet', 'Add one in Files or Sources');
    else {
      var cv = d.selectedCheatVersion && d.selectedCheatVersion !== 'unknown' ? d.selectedCheatVersion : ((g.doc && g.doc.version) || '');
      var gv = d.detectedVersion && d.detectedVersion !== 'unknown' ? d.detectedVersion : '';
      var fmt = String(d.cheatFormat || '').toUpperCase();
      var bad = cv && gv && (d.versionMatch === false || cv !== gv);
      fileRd = rd(bad ? 'warn' : 'ok', bad ? 'warn' : 'check', 'Cheat file', esc((fmt || 'File') + (cv ? ' \u00b7 ' + fmtVer(cv) : '')),
        cv && gv ? esc(bad ? t('File v{a} / game v{b}', { a: cv, b: gv }) : t('Matches v{v}', { v: gv })) : '');
    }
    var eng = S.offline ? rd('warn', 'refresh', 'Engine', 'Reconnecting...', '') : !S.state ? rd('', 'bolt', 'Engine', 'Checking...', '')
      : (S.state.cheats && S.state.cheats.engine) ? rd('ok', 'bolt', 'Engine', 'On', '') : rd('err', 'power', 'Engine', 'Off', 'Turn it on in Settings');
    return gameRd + fileRd + eng;
  }

  // Why the trainer cannot act yet, and the fix. Kept outside the tab body so polls never rebuild the list.
  function paintNotes() {
    var g = S.g, el = $('#gNotes'); if (!g || !el) return;
    var h = '', patches = g.tab === 'patches', ready = g.loaded && !g.err && !g.noFile && g.doc && g.doc.mods && g.doc.mods.length;
    if (g.tab !== 'files') {
      if (S.state && S.state.cheats && !S.state.cheats.engine) {
        h += '<div class="banner err">' + ic('power') + '<div class="txt"><b>The cheat engine is off.</b><span>Cheats and patches cannot be applied until it is turned on.</span></div><a class="btn sm" href="#/settings">Open Settings</a></div>';
      }
      if (S.running !== g.id && (patches || ready)) {
        h += '<div class="banner">' + ic('warn') + '<div class="txt"><b>' + (patches ? 'Start this game to apply patches.' : 'Start this game to use its cheats.') + '</b></div>'
          + '<button class="btn sm ghost" data-act="launch" data-id="' + esc(g.id) + '"' + (S.launchBusy ? ' disabled' : '') + '>' + ic('play', 'fill sm') + 'Play</button></div>';
      }
    }
    if (g.tab === 'cheats' && ready) {
      var anyCrash = Object.keys(g.rows).some(function (k) { return g.rows[k].state === 'crash_suspect'; });
      if (g.stateErr) h += '<div class="banner err">' + ic('warn') + '<div class="txt"><b>Could not read the live cheat state.</b><span>The switches may be out of date.</span></div><button class="btn sm" data-act="retryState">Try again</button></div>';
      if (anyCrash) h += '<div class="banner err">' + ic('warn') + '<div class="txt">A cheat crashed the game last time.</div><button class="btn sm danger" data-act="clearCrash">Clear flag</button></div>';
    }
    if (el.__h !== h) { el.__h = h; el.innerHTML = h; }
  }

  function paintHero() {
    var g = S.g, el = $('#gHero'); if (!g || !el) return;
    var game = findGame(g.id) || { titleId: g.id };
    var live = S.running === g.id, fav = S.fav.has(g.id);
    var ver = game.version && game.version !== 'unknown' ? game.version : '';
    var nm = gname(game);
    var credits = g.doc && Array.isArray(g.doc.credits) ? g.doc.credits.filter(Boolean) : [];
    el.innerHTML = '<div class="t-bg" style="background-image:url(' + esc(gicon(game)) + ')"></div>'
      + '<div class="cover t-cover"><span class="ph" aria-hidden="true">' + esc(nm.charAt(0).toUpperCase()) + '</span><div class="tile-img" style="background-image:url(' + esc(gicon(game)) + ')"></div></div>'
      + '<div class="t-info"><div class="eyebrow">Trainer</div><h1>' + esc(nm) + '</h1><div class="muted mono">' + esc(g.id) + '</div>'
      + '<div class="t-tags"><span class="pill">' + esc(gplat(game)) + '</span>'
      + (ver ? '<span class="pill">' + esc(fmtVer(ver)) + '</span>' : '')
      + (live ? '<span class="pill ok"><i class="dot live"></i>Running</span>' : '<span class="pill">Not running</span>') + '</div>'
      + (credits.length ? '<div class="credits"><span class="credits-l">Credits</span>' + credits.map(function (n) { return '<span class="credit">' + ic('user', 'sm') + esc(n) + '</span>'; }).join('') + '</div>' : '') + '</div>'
      + '<div class="t-act"><button class="icon-btn' + (fav ? ' fav-on' : '') + '" data-act="fav" data-id="' + esc(g.id) + '" aria-label="' + (fav ? 'Remove from favorites' : 'Add to favorites') + '" aria-pressed="' + fav + '">' + ic('star', fav ? 'fill' : '') + '</button>'
      + '<button class="btn' + (live ? ' ghost' : '') + '" data-act="launch" data-id="' + esc(g.id) + '"' + (S.launchBusy ? ' disabled' : '') + '>' + ic('play', 'fill sm') + (live ? 'Relaunch' : 'Play') + '</button></div>';
    var rd = $('#gReady'); if (rd) rd.innerHTML = readyHtml(g);
    paintNotes();
  }

  function viewGame(id, tab) {
    if (!S.g || S.g.id !== id) {
      S.g = newGame(id);
      pushRecent(id);
      loadGamePage(id);
    } else if (S.g.loaded) {
      var keep = S.g;
      loadCheatState(id).then(function () { if (S.g === keep && route().page === 'game' && keep.tab === 'cheats') paintMods(); });
    }
    S.g.tab = tab === 'patches' || tab === 'files' ? tab : 'cheats';
    view.innerHTML = '<button class="back" data-act="back">' + ic('back') + 'All games</button>'
      + '<div class="t-head" id="gHero"></div><div class="ready" id="gReady"></div>'
      + '<div class="tabs" role="tablist">'
      + [['cheats', 'Cheats'], ['patches', 'Patches'], ['files', 'Files']].map(function (x) { return '<button class="tab' + (S.g.tab === x[0] ? ' active' : '') + '" role="tab" aria-selected="' + (S.g.tab === x[0]) + '" data-act="tab" data-t="' + x[0] + '">' + x[1] + '</button>'; }).join('')
      + '</div><div id="gNotes"></div><div id="gBody" role="tabpanel"></div>';
    paintHero();
    paintBody();
    if (S.g.tab === 'patches' && !S.g.patchLoaded) loadPatches();
  }

  function paintBody() {
    var g = S.g, el = $('#gBody'); if (!g || !el) return;
    paintNotes();
    if (g.tab === 'patches') return paintPatches();
    if (g.tab === 'files') return paintFiles();
    if (!g.loaded) { el.innerHTML = skRows(5); return; }
    if (g.err) {
      el.innerHTML = stateBox('err', 'warn', 'Could not load the cheat file', 'The file is there, but it could not be read. It may be damaged or still being copied.',
        '<button class="btn" data-act="retryGame">' + ic('refresh') + 'Try again</button><button class="btn ghost" data-act="tab" data-t="files">Choose another file</button>', errText(g.err));
      padStart(el.querySelector('.btn'));
      return;
    }
    if (g.noFile) {
      el.innerHTML = stateBox('info', 'bolt', 'No cheat file for this game yet', 'Download community cheats from Sources, or add a file from your computer.',
        '<a class="btn" href="#/sources">' + ic('sources') + 'Get cheats</a><button class="btn ghost" data-act="tab" data-t="files">' + ic('upload') + 'Add a file</button>');
      padStart(el.querySelector('.btn'));
      return;
    }
    if (!g.doc || !Array.isArray(g.doc.mods) || !g.doc.mods.length) {
      el.innerHTML = stateBox('warn', 'file', 'This cheat file has no cheats', 'The file loaded but it is empty. Try another file for this game.',
        '<button class="btn" data-act="tab" data-t="files">Choose another file</button>');
      padStart(el.querySelector('.btn'));
      return;
    }
    var d = g.dbg || {}, fname = String(d.cheatPath || '').split('/').pop();
    el.innerHTML = ''
      + '<div class="mods-head">' + (g.doc.mods.length >= 6 ? '<div class="search">' + ic('search') + '<input class="input" data-in="modq" placeholder="Search cheats" aria-label="Search cheats" value="' + esc(g.filter) + '" autocomplete="off"></div>' : '')
      + '<span class="mods-sum" id="modStats"></span><div class="row"><button class="btn soft warn" data-act="disableAll" id="btnDisableAll">' + ic('power', 'sm') + 'Disable all</button>'
      + '<button class="btn soft" data-act="saveAuto" title="Re-apply the cheats that are ON every time this game launches">' + ic('save', 'sm') + 'Save autoload</button>'
      + (g.auto.length ? '<button class="btn quiet" data-act="clearAuto">' + ic('trash', 'sm') + esc(t('Clear autoload ({n})', { n: g.auto.length })) + '</button>' : '') + '</div></div>'
      + (fname ? '<div class="muted small" style="margin:-4px 0 14px"><span class="mono">' + esc(fname) + '</span>' + '</div>' : '')
      + '<div id="modList"></div>';
    paintMods();
  }

  function paintMods() {
    var g = S.g, list = $('#modList'); if (!g || !list || !g.doc) return;
    var mods = g.doc.mods, q = fold(g.filter.trim());
    var notRunning = S.running !== g.id;
    var active = 0;
    Object.keys(g.rows).forEach(function (k) { if (STATE_ON[g.rows[k].state]) active++; });
    var stat = $('#modStats'); if (stat) { stat.textContent = t('{a} of {n} on', { a: active, n: mods.length }); stat.className = 'mods-sum' + (active ? ' has' : ''); }
    var da = $('#btnDisableAll'); if (da) da.hidden = !active;
    paintNotes();

    function row(m, i) {
      var r = g.rows[i] || { state: 'unknown', reason: '', visual: '', canToggle: false, nextOn: null, block: '' };
      var on = r.visual === 'on' || r.visual === 'mixed' || !!STATE_ON[r.state];
      var busy = !!g.applying[i];
      var crash = r.state === 'crash_suspect';
      var dis = busy || notRunning || !r.canToggle;
      var note = '';
      if (crash) note = '<div class="mod-note err">' + ic('warn') + 'Crashed the game last time</div>';
      else if (r.state === 'mixed') note = '<div class="mod-note">' + ic('warn') + 'Partly applied</div>';
      else if (r.state === 'conflict') note = '<div class="mod-note">' + ic('warn') + esc(r.reason || 'Conflicts with another cheat') + '</div>';
      else if (!on && (r.state === 'mismatch' || r.state === 'address_unresolved')) note = '<div class="mod-note">' + ic('warn') + 'May not match this game version</div>';
      else if (!notRunning && !r.canToggle && !on) note = '<div class="mod-note lock">' + ic('lock') + esc(r.block || 'This cheat cannot be toggled right now.') + '</div>';
      return '<div class="mod' + (on ? ' is-on' : '') + (crash ? ' is-err' : '') + (notRunning ? ' is-off-lock' : '') + '"><span class="mod-ic">' + ic('bolt') + '</span><div class="mod-main"><div class="mod-name">' + esc(m.name || t('Cheat #{n}', { n: i })) + '</div>'
        + (m.description ? '<div class="mod-desc">' + esc(m.description) + '</div>' : '') + note + '</div>'
        + '<button class="switch' + (on ? ' on' : '') + (busy ? ' busy' : '') + '" data-act="toggle" data-i="' + i + '"' + (dis ? ' disabled' : '')
        + ' role="switch" aria-checked="' + on + '" aria-label="' + esc(m.name || 'Cheat') + '"></button></div>';
    }
    var cap = q ? Infinity : (g.modShown || MOD_CHUNK), left = cap, hidden = 0;
    function group(title, items) {
      var f = q ? items.filter(function (x) { return fold(x.m.name).indexOf(q) >= 0; }) : items;
      var part = f.slice(0, Math.max(0, left)); left -= part.length; hidden += f.length - part.length;
      return part.length ? (title ? '<div class="group-title">' + title + '</div>' : '') + part.map(function (x) { return row(x.m, x.i); }).join('') : '';
    }
    var tog = [], btn = [];
    mods.forEach(function (m, i) { (String(m.type || 'checkbox').toLowerCase() === 'button' ? btn : tog).push({ m: m, i: i }); });
    var both = tog.length && btn.length;
    var html = group(both ? 'Toggles' : '', tog) + group(both ? 'Actions' : '', btn);
    if (hidden) html += '<div class="grid-more"><button class="btn ghost" data-act="modMore">' + esc(t('Show more ({n})', { n: hidden })) + '</button></div>';
    html = html || stateBox('', 'search', esc(t('No cheats match "{q}".', { q: g.filter })), '', '');
    if (list.__h !== html) { list.__h = html; withFocus(list, function () { list.innerHTML = html; }); }
    var last = g.lastSw != null ? list.querySelector('.switch[data-i="' + g.lastSw + '"]:not([disabled])') : null;
    padStart(last || list.querySelector('.switch:not([disabled])') || $('#gHero .btn'));
  }

  var TOGGLE_ERR = {
    app_not_stable: 'The game is still starting. Try again in a few seconds.',
    apply_in_progress: 'Another cheat is being applied. Try again in a moment.',
    crash_suspect: 'This cheat crashed the game before. Clear the flag to retry.',
    game_not_running: 'Start the game first.', base_not_ready: 'The game is still loading.'
  };
  async function toggleMod(i, sw) {
    var g = S.g; if (!g || g.applying[i]) return;
    var r = g.rows[i];
    if (!r || !r.canToggle) { toast(r && r.block ? r.block : 'This cheat cannot be toggled right now.', 'warn'); return; }
    var next = r.nextOn;
    g.applying[i] = 1;
    // Flip the switch right away; a failed apply repaints it back.
    if (sw) {
      sw.classList.add('busy'); sw.classList.toggle('on', !!next); sw.setAttribute('aria-checked', next ? 'true' : 'false');
      if (sw.parentNode) sw.parentNode.classList.toggle('is-on', !!next);
    }
    try {
      await get('/api/cheats/toggle?titleId=' + encodeURIComponent(g.id) + '&index=' + i + '&on=' + (next ? 1 : 0), 20000);
    } catch (e) {
      toast(TOGGLE_ERR[e.code] || e.message || 'Could not toggle the cheat.', e.code === 'app_not_stable' || e.code === 'apply_in_progress' ? 'warn' : 'error');
    }
    await loadCheatState(g.id);
    delete g.applying[i];
    if (S.g === g) paintBody();
  }

  /* ----------------------------------------------------------- files tab */
  function paintFiles() {
    var g = S.g, el = $('#gBody'); if (!g || !el) return;
    var d = g.dbg || {}, cands = Array.isArray(d.candidates) ? d.candidates : [];
    var manual = d.selectionMode === 'manual';
    var html = '<div class="panel">' + sect('file', 'Cheat files for this game', 'Saved in /data/cheatrunner/cheats. The file marked In use is the one the trainer reads.',
      (manual ? '<button class="btn sm ghost" data-act="autoSelect">Auto-select</button>' : ''));
    if (!cands.length) html += stateBox('', 'file', 'No local file found', 'Download one from Sources or drop a file below.', '<a class="btn sm" href="#/sources">' + ic('sources', 'sm') + 'Get cheats</a>');
    cands.forEach(function (c) {
      var name = String(c.filename || c.path || '').split('/').pop();
      var wrong = c.match === 'wrong_version';
      html += '<div class="item' + (c.selected ? ' sel' : '') + '"><div class="item-main"><div class="item-title">' + esc(name) + '</div>'
        + '<div class="item-sub">' + esc([String(c.format || '').toUpperCase(), c.version && c.version !== 'unknown' ? 'v' + c.version : ''].filter(Boolean).join(' - ')) + '</div></div>'
        + (wrong ? '<span class="pill warn">' + ic('warn', 'sm') + 'Other version</span>' : c.match === 'exact' ? '<span class="pill ok">' + ic('check', 'sm') + 'Exact</span>' : '')
        + (c.selected ? '<span class="pill on">In use</span>' : '<button class="btn sm ghost" data-act="selectFile" data-path="' + esc(c.path || '') + '" data-wrong="' + (wrong ? 1 : 0) + '">Use</button>') + '</div>';
    });
    if (cands.length && g.doc) html += '<div class="row" style="margin-top:10px;justify-content:flex-end"><button class="btn sm quiet" data-act="deleteLocal">' + ic('trash', 'sm') + 'Delete the file in use</button></div>';
    html += '</div><div class="panel">' + sect('upload', 'Add cheats', 'A .mc4, .shn or .json file from your computer.')
      + '<div class="drop" data-act="pickUpload" id="dropZone" role="button" tabindex="0">' + ic('upload') + 'Drop a .mc4, .shn or .json file here, or click to browse</div></div>';
    el.innerHTML = html;
  }
  async function uploadFile(file) {
    var g = S.g; if (!g || !file) return;
    if (!/\.(mc4|shn|json)$/i.test(file.name)) { toast('Use a .mc4, .shn or .json file.', 'warn'); return; }
    try {
      var res = await fetch('/api/cheats/upload?titleId=' + encodeURIComponent(g.id) + '&filename=' + encodeURIComponent(file.name), { method: 'POST', body: await file.arrayBuffer(), cache: 'no-store' });
      if (!res.ok) { var j = null; try { j = await res.json(); } catch (e) {} toast((j && (j.message || j.error)) || 'Upload failed.', 'error'); return; }
      toast('Cheat file added.', 'success');
      g.loaded = false; await loadGames(true); await loadGamePage(g.id);
    } catch (e) { toast('Upload failed.', 'error'); }
  }

  /* --------------------------------------------------------- patches tab */
  async function loadPatches(quiet) {
    var g = S.g; if (!g) return;
    var el = $('#gBody'); if (el && !quiet) el.innerHTML = skRows(4);
    try {
      g.patch = await get('/api/patches?titleId=' + encodeURIComponent(g.id));
      if (!quiet) try { var a = await get('/api/patches/profile?titleId=' + encodeURIComponent(g.id), { retries: 0 }); g.patchAuto = Array.isArray(a.entryIds) ? a.entryIds : []; } catch (e) { g.patchAuto = []; }
    } catch (e) { if (quiet) return; g.patch = { error: errText(e) || 'error' }; }
    g.patchLoaded = true;
    if (S.g === g && g.tab === 'patches') paintPatches();
  }
  function paintPatches() {
    var g = S.g, el = $('#gBody'), d = g && g.patch; if (!el) return;
    if (!d) { el.innerHTML = skRows(4); return; }
    if (d.error) {
      el.innerHTML = stateBox('err', 'warn', 'Could not load the patches', 'The patch files for this game could not be read. Try again in a moment.',
        '<button class="btn" data-act="retryPatches">' + ic('refresh') + 'Try again</button>', d.error);
      return;
    }
    var entries = d.entries || [];
    if (!d.hasPatchFile || !entries.length) {
      el.innerHTML = stateBox('info', 'file', 'No patches for this game', 'Download the official patch set from Sources, or put your XML patches in /data/cheatrunner/patches/xml.',
        '<a class="btn" href="#/sources">' + ic('sources') + 'Get patches</a>');
      return;
    }
    var html = '<div class="banner info">' + ic('info') + '<div class="txt">Some patches only work if applied at game start: save the autoload and restart the game.</div>'
      + '<button class="btn sm soft" data-act="patchSaveAuto">' + ic('save', 'sm') + 'Save autoload</button>'
      + (g.patchAuto.length ? '<button class="btn sm quiet" data-act="patchClearAuto">' + ic('trash', 'sm') + esc(t('Clear autoload ({n})', { n: g.patchAuto.length })) + '</button>' : '') + '</div>';
    var pshown = Math.min(entries.length, g.patchShown || PATCH_CHUNK);
    entries.slice(0, pshown).forEach(function (e, i) {
      var vs = e.versionStatus || '', br = e.blockReason || '';
      var ver = vs === 'mask' ? t('Any version') : vs === 'match' ? t('v{v} matches', { v: e.appVer }) : vs === 'mismatch' ? t('Needs v{v}', { v: e.appVer }) : e.appVer ? 'v' + e.appVer : t('Any version');
      var act;
      if (e.applied) act = '<span class="pill ok">' + ic('check', 'sm') + 'Applied</span><button class="btn sm ghost" data-act="patchRestore" data-i="' + i + '">Restore</button>';
      else if (e.canApply) act = '<button class="btn sm" data-act="patchApply" data-i="' + i + '">Apply</button>';
      else if (br === 'version_mismatch') act = '<span class="pill warn">' + ic('warn', 'sm') + 'Other version</span><button class="btn sm ghost" data-act="patchForce" data-i="' + i + '">Force apply</button>';
      else act = '<span class="muted small">' + esc(br === 'missing_game' ? 'Start the game' : br === 'unsupported_line_type' ? 'Unsupported patch' : br.replace(/_/g, ' ')) + '</span>';
      html += '<div class="item' + (e.applied ? ' ok' : '') + '"><div class="item-main"><div class="item-title">' + esc(e.name) + '</div>'
        + '<div class="item-meta">' + (e.author ? '<span class="item-author">' + ic('user', 'sm') + esc(t('by {a}', { a: e.author })) + '</span>' : '') + '<span class="item-ver">' + esc(ver) + '</span></div>'
        + (e.note ? '<div class="item-note">' + esc(e.note) + '</div>' : '') + '</div>' + act + '</div>';
    });
    if (pshown < entries.length) html += '<div class="grid-more"><button class="btn ghost" data-act="patchMore">' + esc(t('Show more ({n})', { n: entries.length - pshown })) + '</button></div>';
    if (el.__h !== html) { el.__h = html; el.innerHTML = html; }
  }
  var PATCH_CHUNK = isPS5 ? 25 : 60;
  var MOD_CHUNK = isPS5 ? 30 : 200;
  async function patchCall(kind, i, force) {
    var g = S.g, e = g && g.patch && g.patch.entries && g.patch.entries[i]; if (!e) return;
    var id = e.entryId ? '&entryId=' + encodeURIComponent(e.entryId) : '&index=' + i;
    var btn = $('[data-act="' + (kind === 'restore' ? 'patchRestore' : force ? 'patchForce' : 'patchApply') + '"][data-i="' + i + '"]');
    if (btn) btn.disabled = true;
    try {
      var r = await get('/api/patches/' + kind + '?titleId=' + encodeURIComponent(g.id) + id + (force ? '&force=1' : ''), 20000);
      if (r && r.ok) { await loadPatches(true); }
      else { toast((r && (r.message || r.error)) || 'Failed.', 'error'); if (btn) btn.disabled = false; }
    } catch (ex) { toast(ex.message || 'Failed.', 'error'); if (btn) btn.disabled = false; }
  }

  /* --------------------------------------------------------------- sources */
  var repoPolling = false;
  function viewSources() {
    view.innerHTML = stage('Downloads', 'Sources', 'Download ready-made cheats and patches for your games. Downloads keep running while you browse.')
      + '<div class="src-cols"><div class="panel">' + sect('sources', 'Community cheat repositories', 'Download ready-made cheat files for your games. They are saved to /data/cheatrunner/cheats, and downloads keep running in the background.')
      + '<div class="row" style="margin-bottom:8px"><select class="select grow" id="repoSource" aria-label="Cheat source"><option value="hencollection">HEN Cheats Collection (' + t('Recommended') + ')</option><option value="ps5cheats">PS5 Cheats</option><option value="goldhen">GoldHEN Cheats</option><option value="henppsa">HEN PPSA Cheats</option></select></div>'
      + '<div class="row"><label class="row small muted chk-row"><input type="checkbox" class="chk" id="repoOverwrite">Also update files that changed</label></div>'
      + '<div class="row" style="margin-top:12px"><button class="btn half" data-act="repoGo">' + ic('sources') + 'Download selected</button><button class="btn ghost half" data-act="repoAll">Download all sources</button></div>'
      + '<div id="repoOut"></div>'
      + '<div class="src-link"><div class="grow"><b>Browse cheats online</b><div class="muted small">Search every game with cheats and check if yours is covered.</div></div>'
      + '<a class="btn ghost" href="https://hencheats.vercel.app/" target="_blank" rel="noopener noreferrer">' + ic('search', 'sm') + 'Open site</a></div></div>'
      + '<div class="panel">' + sect('file', 'Community patch repositories', 'Download community patch sets. Patches are saved to /data/cheatrunner/patches.', '', 'info')
      + '<div class="row" style="margin-bottom:8px"><select class="select grow" id="patchSource" aria-label="Patch source"><option value="crpatches">CheatRunner Patches</option><option value="psgamepatch">PS-Game-Patch</option></select></div>'
      + '<div class="row"><label class="row small muted chk-row"><input type="checkbox" class="chk" id="patchOverwrite">Also update files that changed</label></div>'
      + '<div class="row" style="margin-top:12px"><button class="btn half" data-act="patchRepoGo">' + ic('sources') + 'Download patches</button>'
      + '<a class="btn ghost half" href="https://github.com/notmaj0r/CheatRunner_Patches" target="_blank" rel="noopener noreferrer">' + ic('github', 'sm') + 'Open repository</a></div>'
      + '<div id="repoOutP"></div></div></div>';
    paintRepo(S.dl);
    get('/api/cheats/repo/download/status', { timeoutMs: 4000, retries: 0 }).then(function (st) { S.dl = st; paintRepo(st); paintDlChip(st); if (st && st.state === 'running') watchDownload(); }).catch(function () {});
  }
  function isPatchSrc(s) { return s === 'psgamepatch' || s === 'crpatches'; }
  var REPO_NAMES = { hencollection: 'HEN Cheats Collection', ps5cheats: 'PS5 Cheats', goldhen: 'GoldHEN Cheats', henppsa: 'HEN PPSA Cheats', psgamepatch: 'PS-Game-Patch', crpatches: 'CheatRunner Patches', all: 'all sources' };
  function fmtBytes(n) {
    if (!n) return '0 KB';
    if (n < 1048576) return Math.max(1, Math.round(n / 1024)) + ' KB';
    return (n / 1048576).toFixed(n < 10485760 ? 2 : 1) + ' MB';
  }
  function fmtSpan(sec) {
    sec = Math.round(sec);
    if (sec < 0 || !isFinite(sec)) return 'â';
    if (sec < 60) return t('{s}s', { s: sec });
    return t('{m}m {s}s', { m: Math.floor(sec / 60), s: (sec % 60 < 10 ? '0' : '') + (sec % 60) });
  }
  function dlStat(id, icon, label) { return '<div class="dl-stat"><span>' + ic(icon, 'sm') + label + '</span><b id="' + id + '">â</b></div>'; }
  var dlBuilt = false;
  function paintRepo(st) {
    var forPatches = !!st && isPatchSrc(st.source);
    var out = $(forPatches ? '#repoOutP' : '#repoOut'), other = $(forPatches ? '#repoOut' : '#repoOutP'); if (!out) return;
    if (other && other.firstChild) { other.innerHTML = ''; dlBuilt = false; }
    if (!st || !st.state || st.state === 'idle') { out.innerHTML = ''; dlBuilt = false; return; }
    if (!dlBuilt || !$('#dlBar')) {
      out.innerHTML = '<div class="dl" id="dl"><div class="dl-top"><div><div class="dl-title" id="dlTitle"></div><div class="dl-sub" id="dlSub"></div></div><div class="dl-pct" id="dlPct">0%</div></div>'
        + '<div class="progress dl-bar" role="progressbar" aria-valuemin="0" aria-valuemax="100" id="dlTrack"><i id="dlBar"></i></div>'
        + '<div class="dl-stats">' + dlStat('dlSpeed', 'gauge', 'Speed') + dlStat('dlEta', 'refresh', 'Time left') + dlStat('dlFiles', 'file', 'Files')
        + dlStat('dlBytes', 'sources', 'Downloaded') + dlStat('dlSame', 'check', 'Up to date') + dlStat('dlFail', 'warn', 'Failed') + '</div>'
        + '<div class="dl-file mono" id="dlFile"></div><div class="dl-res" id="dlRes"></div></div>';
      dlBuilt = true;
    }
    var run = st.state === 'running', arch = run && st.phase === 'archive';
    var pct = st.state === 'done' ? 100 : Math.min(100, Number(st.pct || 0));
    var same = (st.skipped || 0) + (st.unchanged || 0), processed = (st.downloaded || 0) + same + (st.failed || 0);
    var name = st.source === 'all' ? t('all sources') : REPO_NAMES[st.source] || st.source || 'cheats';
    var multi = st.sourceTotal > 1 ? t('Source {i} of {n}', { i: st.sourceIndex, n: st.sourceTotal }) + ' \u00b7 ' : '';
    var PH = { listing: ['Checking {name}', 'Reading the file list from GitHub'], archive: ['Downloading {name}', 'Fetching the whole repository in one go'],
      extract: ['Unpacking {name}', forPatches ? 'Saving the patch files' : 'Saving the cheat files'], downloading: ['Downloading {name}', 'Comparing and downloading files one by one'] };
    var ph = PH[st.phase] || PH.downloading;
    $('#dl').className = 'dl' + (run ? ' run' : st.state === 'done' ? ' done' : ' err') + (arch ? ' ind' : '');
    $('#dlTitle').textContent = run ? t(ph[0], { name: name }) : st.state === 'done' ? t('Finished {name}', { name: name }) : t('Download failed');
    $('#dlSub').textContent = run ? multi + t(ph[1]) : t('Took {t}', { t: fmtSpan(st.elapsedSec || 0) });
    $('#dlPct').textContent = arch ? fmtBytes(st.bytes || 0) : pct + '%';
    $('#dlBar').style.width = arch ? '' : pct + '%';
    var tr = $('#dlTrack'); if (tr) { if (arch) tr.removeAttribute('aria-valuenow'); else tr.setAttribute('aria-valuenow', pct); tr.setAttribute('aria-label', $('#dlTitle').textContent); }
    $('#dlSpeed').textContent = run && st.bytesPerSec > 0 ? fmtBytes(st.bytesPerSec) + '/s' : st.bytes > 0 && !run ? t('avg {s}', { s: fmtBytes(st.bytesPerSec) + '/s' }) : '\u2014';
    $('#dlEta').textContent = run ? fmtSpan(st.etaSec) : '\u2014';
    $('#dlFiles').textContent = processed + (st.total ? ' / ' + st.total : '');
    $('#dlBytes').textContent = fmtBytes(st.bytes || 0);
    $('#dlSame').textContent = String(same);
    $('#dlFail').textContent = String(st.failed || 0);
    $('#dlFile').textContent = run && st.current ? st.current : '';
    // Finished: say what happened and what to do next. Failed: a plain reason and a retry.
    $('#dlRes').innerHTML = st.state === 'done'
      ? '<span class="pill ok">' + ic('check', 'sm') + (st.downloaded ? t('{n} new or updated file(s) saved', { n: st.downloaded }) : t('Everything is already up to date')) + '</span>'
        + (forPatches ? '' : '<button class="btn sm" data-act="goGames">' + ic('games', 'sm') + 'See your games</button>')
      : st.state === 'error' ? '<span class="pill err">' + ic('warn', 'sm') + esc(st.error || t('Download failed')) + '</span><button class="btn sm" data-act="repoRetry">' + ic('refresh', 'sm') + 'Try again</button>' : '';
  }
  // Shows the running download in the top bar from any page; paints the panel only on Sources.
  function paintDlChip(st) {
    var run = !!st && st.state === 'running', chip = $('#dlChip'), dot = $('#dotSources');
    if (chip) chip.hidden = !run;
    if (dot) dot.hidden = !run;
    if (!run || !chip) return;
    var arch = st.phase === 'archive', pct = Math.min(100, Number(st.pct || 0));
    $('#dlChipTxt').textContent = arch ? t('Downloading') : t('Downloading {p}%', { p: pct });
    $('#dlChipBar').style.width = arch ? '40%' : pct + '%';
  }
  async function watchDownload() {
    if (repoPolling) return; repoPolling = true;
    try {
      for (;;) {
        var st = await get('/api/cheats/repo/download/status', { timeoutMs: 8000, retries: 0 }).catch(function () { return null; });
        if (st) S.dl = st;
        paintDlChip(st);
        if (route().page === 'sources') paintRepo(st);
        if (st && st.state === 'running') S.dlSawRun = true;
        if (!st || st.state !== 'running') {
          if (st && S.dlSawRun) {
            S.dlSawRun = false;
            if (st.state === 'done') toast(st.downloaded ? t('{n} new or updated file(s) saved', { n: st.downloaded }) : t('Everything is already up to date'), 'success');
            else if (st.state === 'error') toast(t('Download failed'), 'error');
            if (st.state === 'done' && !isPatchSrc(st.source)) loadGames(true);
          }
          break;
        }
        await sleep(isPS5 ? 2500 : 1200);
      }
    } finally { repoPolling = false; }
  }
  async function repoStart(all, patches) {
    var src = patches ? ($('#patchSource') || {}).value || 'crpatches' : all ? 'all' : ($('#repoSource') || {}).value || 'hencollection';
    var ovr = ($(patches ? '#patchOverwrite' : '#repoOverwrite') || {}).checked ? 1 : 0;
    S.dlLast = { all: all, patches: patches };
    try {
      var j = await post('/api/cheats/repo/download?source=' + encodeURIComponent(src) + '&overwrite=' + ovr);
      if (j && (j.state === 'running' || j.ok)) { S.dlSawRun = true; watchDownload(); } else toast((j && (j.message || j.error)) || 'Could not start.', 'error');
    } catch (e) { if (e.body && e.body.state === 'running') watchDownload(); else toast(e.message || 'Could not start the download.', 'error'); }
  }

  /* ---------------------------------------------------------------- system */
  var logs = { seq: -1, list: [], min: 0, q: '', atEnd: true };

  function viewSystem() {
    view.innerHTML = stage('Health', 'System', 'Live health of the cheat engine and memory patching, and what CheatRunner has been doing.',
        '<button class="btn sm ghost" data-act="qr">' + ic('qr', 'sm') + 'QR code</button><button class="btn sm ghost" data-act="copyUrl">' + ic('link', 'sm') + 'Copy URL</button>')
      + '<div id="healthBanner"></div><div class="panel"><div class="kv" id="statusKv"></div></div>'
      + '<div class="panel">' + sect('terminal', 'Logs', 'What CheatRunner has been doing - useful when something does not work.', '', 'info')
      + '<div class="log-tools"><div class="seg" role="group" aria-label="Log level filter">'
      + [[0, 'All'], [2, 'Warnings and errors'], [3, 'Errors only']].map(function (o) { return '<button class="seg-b' + (logs.min === o[0] ? ' on' : '') + '" data-act="logLvl" data-v="' + o[0] + '" aria-pressed="' + (logs.min === o[0]) + '">' + o[1] + '</button>'; }).join('')
      + '</div><input class="input" data-in="logq" placeholder="Filter" aria-label="Filter logs" value="' + esc(logs.q) + '" autocomplete="off">'
      + '<button class="btn sm ghost" data-act="copyLogs">' + ic('copy', 'sm') + 'Copy all</button></div>'
      + '<div class="console" id="logOut" role="log" aria-live="off" tabindex="0" aria-label="Logs"><span class="muted">Loading logs...</span></div>'
      + '<div class="log-foot"><span id="logFollow"></span><button class="btn sm quiet" id="logJump" data-act="logJump" hidden>' + ic('back', 'sm') + 'Jump to latest</button></div>'
      + '<div class="row" style="margin-top:8px"><button class="btn sm danger" data-act="shutdown" id="btnShutdown" hidden>' + ic('power', 'sm') + 'Shut down payload</button></div></div>'
      + '<div class="panel">' + sect('info', 'About', '')
      + '<div class="about-brand"><img src="/CheatRunner.png" alt="" width="64" height="64"><div><div class="about-name">CheatRunner</div><span class="ver-text">v' + esc(String((S.state || {}).version || '').replace(/^v/, '')) + '</span></div></div>'
      + '<p class="about-tag">CheatRunner - Cheat Trainer for your PS5!</p>'
      + '<p class="muted small" style="margin-top:4px">Made by maj0r - \ud83c\udde7\ud83c\uddf7</p>'
      + '<p class="muted small" style="margin-top:8px">For users. By users.</p>'
      + '<div style="margin-top:16px;padding-top:14px;border-top:1px solid var(--border)">'
      + '<div class="muted small" style="margin-bottom:6px">Source</div>'
      + '<a class="link-card" href="https://github.com/notmaj0r/CheatRunner" target="_blank" rel="noopener noreferrer">'
      + '<span class="link-card-ic">' + ic('github') + '</span><span class="link-card-txt"><b>CheatRunner</b><small>Leave the project a star! \u2b50</small></span>' + ic('chev', 'sm')
      + '</a></div></div>';
    paintStatus(); refreshLogs();
    var lo = $('#logOut'); if (lo) lo.addEventListener('scroll', logScroll, { passive: true });
  }
  // Follows the newest line while the reader is at the bottom; offers a jump back when they scroll up.
  function logScroll() {
    var out = $('#logOut'); if (!out) return;
    logs.atEnd = out.scrollTop + out.clientHeight >= out.scrollHeight - 30;
    paintFollow();
  }
  function paintFollow() {
    var f = $('#logFollow'), j = $('#logJump');
    if (f) f.textContent = logs.atEnd ? t('Following the latest lines') : t('Paused while you read');
    if (j) j.hidden = logs.atEnd;
  }
  function paintStatus() {
    var kv = $('#statusKv'); if (!kv) return;
    var s = S.state || {}, c = s.cheats || {}, p = s.privileges || {};
    var cheatGames = S.games.filter(function (g) { return g.hasCheat; }).length, patchGames = S.games.filter(function (g) { return g.hasPatch; }).length;
    function cell(k, v, cls) { return '<div class="' + (cls || '') + '"><small>' + (cls ? '<i class="dot"></i>' : '') + k + '</small><b>' + esc(v) + '</b></div>'; }
    var privOk = !s.privileges || (p.uid_ok && p.authid_ok);
    kv.innerHTML = cell('Cheat engine', c.engine ? 'On' : 'Off', c.engine ? 'cool' : 'hot')
      + cell('Memory patching', c.canPatchGameMemory ? 'Ready' : 'Not ready', c.canPatchGameMemory ? 'cool' : 'warm')
      + (s.privileges ? cell('Privileges', privOk ? 'OK' : 'Missing', privOk ? 'cool' : 'warm') : '')
      + cell('Games with cheats', cheatGames + ' / ' + S.games.length) + cell('Games with patches', patchGames + ' / ' + S.games.length)
      + cell('Version', 'v' + String(s.version || '').replace(/^v/, ''));
    var good = !!c.engine && !!c.canPatchGameMemory && privOk, hb = $('#healthBanner');
    if (hb) hb.innerHTML = !S.state ? '' : '<div class="banner ' + (good ? 'ok' : '') + '">' + ic(good ? 'check' : 'warn') + '<div class="txt"><b>' + (good ? 'Everything looks good.' : 'Something needs attention - check the details below.') + '</b></div>'
      + (!c.engine ? '<a class="btn sm" href="#/settings">Open Settings</a>' : '') + '</div>';
    var sd = $('#btnShutdown'); if (sd) sd.hidden = !(s.dev && s.dev.reloadEnabled);
  }
  function logLine(x) { return '[' + String(x.level || 'info').toLowerCase() + '] [' + (x.tag || 'core') + '] ' + (x.message || ''); }
  function renderLogs() {
    var out = $('#logOut'); if (!out) return;
    var q = fold(logs.q), rank = { debug: 0, info: 1, warn: 2, error: 3 };
    var list = logs.list.filter(function (x) {
      var lv = String(x.level || 'info').toLowerCase();
      return (rank[lv] == null ? 1 : rank[lv]) >= logs.min && (!q || fold(logLine(x)).indexOf(q) >= 0);
    }).slice(-300);
    var atEnd = logs.atEnd;
    out.innerHTML = list.map(function (x) { return '<div class="' + esc(String(x.level || 'info').toLowerCase()) + '">' + esc(logLine(x)) + '</div>'; }).join('')
      || '<span class="muted">' + (logs.list.length ? 'No matching lines.' : 'No logs yet.') + '</span>';
    if (atEnd) out.scrollTop = out.scrollHeight;
    paintFollow();
  }
  async function refreshLogs() {
    var out = $('#logOut'); if (!out) return;
    try {
      var d = await get(logs.seq >= 0 ? '/api/logs?since=' + logs.seq + '&limit=200' : '/api/logs?limit=200', { timeoutMs: 8000, retries: 0 });
      var last = typeof d.lastSeq === 'number' ? d.lastSeq : -1;
      if (last >= 0 && logs.seq >= 0 && last < logs.seq) { logs.list = []; logs.seq = -1; }
      var prev = logs.seq;
      var chunk = (d.logs || []).filter(function (x) { return prev < 0 || typeof x.seq !== 'number' || x.seq > prev; });
      logs.list = prev < 0 ? chunk.slice() : logs.list.concat(chunk);
      if (last >= 0) logs.seq = last;
      if (logs.list.length > 1500) logs.list = logs.list.slice(-1500);
      renderLogs();
    } catch (e) {}
  }

  /* -------------------------------------------------------------- settings */
  var THEMES = [['dark', 'Dark', '#0a0e16', '#10161f', '#e11d48'], ['crimson', 'Crimson', '#0f0a0c', '#171013', '#e11d48'], ['midnight', 'Midnight', '#070b16', '#0d1424', '#3b82f6'],
    ['oled', 'OLED', '#000', '#0b0b0c', '#e11d48'], ['light', 'Light', '#f4f6fa', '#ffffff', '#d6123f']];
  function applyTheme(t) {
    var ok = THEMES.some(function (x) { return x[0] === t; });
    document.documentElement.setAttribute('data-theme', ok ? t : 'midnight');
    store('cr_theme', ok ? t : 'midnight');
  }
  var saveTimer = 0;
  function saveState(text, cls) {
    var el = $('#saveState'); if (!el) return;
    el.textContent = text; el.className = 'save-state show ' + (cls || '');
    clearTimeout(saveTimer);
    if (cls !== 'err') saveTimer = setTimeout(function () { el.classList.remove('show'); }, 1600);
  }
  async function setCfg(key, val) {
    saveState('Saving...', '');
    try { await post('/api/config/set?key=' + encodeURIComponent(key) + '&value=' + encodeURIComponent(String(val))); S.cfg[key] = val; saveState('Saved', 'ok'); return true; }
    catch (e) { saveState(e.message || 'Could not save. Try again.', 'err'); return false; }
  }
  async function viewSettings() {
    view.innerHTML = stage('Preferences', 'Settings', 'Appearance, the cheat engine and controller options. Changes are saved as you make them.', '<span class="save-state" id="saveState" role="status" aria-live="polite"></span>')
      + '<div class="panel">' + skRows(4) + '</div>';
    var failed = null;
    try { S.cfg = await get('/api/config', { retries: 1 }); applyTheme(S.cfg.theme); } catch (e) { failed = e; }
    if (route().page !== 'settings') return;
    if (failed) {
      view.innerHTML = stage('Preferences', 'Settings', '')
        + stateBox('err', 'warn', 'Could not load the settings', 'The current values could not be read, so nothing is shown rather than showing wrong ones.', '<button class="btn" data-act="retrySettings">' + ic('refresh') + 'Try again</button>', errText(failed));
      return;
    }
    var c = S.cfg;
    function sw(key, title, hint) { return '<div class="set-row sw"><div class="set-txt"><b>' + title + '</b><span>' + hint + '</span></div><div class="set-ctl"><button class="switch' + (c[key] ? ' on' : '') + '" data-act="cfgSwitch" data-k="' + key + '" role="switch" aria-checked="' + (c[key] ? 'true' : 'false') + '" aria-label="' + esc(title) + '"></button></div></div>'; }
    function num(key, title, hint, min, max) { return '<div class="set-row"><div class="set-txt"><b>' + title + '</b><span>' + hint + '</span></div><div class="set-ctl"><input class="input" type="number" data-change="cfgNum" data-k="' + key + '" min="' + min + '" max="' + max + '" value="' + Number(c[key] || 0) + '"></div></div>'; }
    var cur = THEMES.some(function (x) { return x[0] === c.theme; }) ? c.theme : 'midnight', lite = rootCls.contains('lite');
    view.innerHTML = stage('Preferences', 'Settings', 'Appearance, the cheat engine and controller options. Changes are saved as you make them.', '<span class="save-state" id="saveState" role="status" aria-live="polite"></span>')
      + '<div class="panel">' + sect('settings', 'Appearance', 'Theme, effects and language of this dashboard.')
      + '<div class="themes" role="radiogroup" aria-label="Theme">' + THEMES.map(function (x) {
        return '<button class="theme-opt' + (cur === x[0] ? ' on' : '') + '" role="radio" aria-checked="' + (cur === x[0]) + '" data-act="themeSet" data-v="' + x[0] + '"><span class="theme-prev" style="background:' + x[2] + '"><i class="p-rail" style="background:' + x[3] + '"></i><i class="p-card" style="background:' + x[3] + '"></i><i class="p-acc" style="background:' + x[4] + '"></i></span>'
          + '<span class="theme-name">' + x[1] + ic('check', 'sm') + '</span></button>'; }).join('') + '</div>'
      + '<div class="set-row"><div class="set-txt"><b>Visual effects</b><span>Reduced turns off blur, shadows and animation. This is the default in the PS5 browser.</span></div><div class="set-ctl"><div class="seg" role="group" aria-label="Visual effects">'
      + [['full', 'Full'], ['reduced', 'Reduced']].map(function (x) { var on = (lite ? 'reduced' : 'full') === x[0]; return '<button class="seg-b' + (on ? ' on' : '') + '" data-act="fxSet" data-v="' + x[0] + '" aria-pressed="' + on + '">' + x[1] + '</button>'; }).join('') + '</div></div></div>'
      + '<div class="set-row"><div class="set-txt"><b>Language</b><span>Language of this dashboard.</span></div><div class="set-ctl lang"><button class="btn ghost lang-btn" data-act="langPick"><img class="flag" src="' + CRI.flag(CRI.lang) + '" alt="">' + esc(langName(CRI.lang)) + '</button></div></div></div>'
      + '<div class="panel">' + sect('bolt', 'Cheat engine', 'The essentials for applying cheats and patches.')
      + sw('cheat_engine', 'Cheat engine', 'Master switch for applying cheats and patches.')
      + sw('tile_autoinstall_enabled', 'Home-screen tile', 'Install the CheatRunner tile on the PS5 home screen at boot.')
      + sw('cheat_mark_crash_suspect', 'Crash detection', 'Flag a cheat if the game closes right after it was turned on.')
      + '</div>'
      + hotkeyCard(c)
      + '<div class="panel"><details class="adv"><summary>Advanced</summary>'
      + num('cheat_min_stable_ms', 'Wait after game start (ms)', 'Cheats are held back this long after a game launches.', 0, 60000)
      + num('cheat_apply_cooldown_ms', 'Gap between cheats (ms)', 'Minimum time between two cheat toggles.', 0, 10000)
      + sw('cheat_master_code_fixup', 'Master-code fixup', 'Resolve master-code dependent cheats automatically.')
      + '<div class="set-row"><div class="set-txt"><b>Log level</b><span>How much detail is written to the log.</span></div><div class="set-ctl"><select class="select" data-change="cfgLog" aria-label="Log level">'
      + ['debug', 'info', 'warn', 'error'].map(function (l) { return '<option' + (c.log_level === l ? ' selected' : '') + '>' + l + '</option>'; }).join('') + '</select></div></div>'
      + '</details></div>'
      + '<div class="panel">' + sect('flag', 'Troubleshooting', 'Send a report when something does not work.')
      + '<div class="set-row"><div class="set-txt"><b>Report an issue</b><span>Copies the diagnostics and opens the issue page on GitHub.</span></div><div class="set-ctl"><button class="btn ghost" data-act="reportIssue">' + ic('flag', 'sm') + 'Report an issue</button></div></div></div>'
      + '<div class="panel danger"><div class="danger-head">' + ic('warn') + 'Danger zone</div>'
      + '<div class="set-row"><div class="set-txt"><b>Reset to defaults</b><span>Cheats, patches and autoload profiles are not touched.</span></div><div class="set-ctl"><button class="btn danger" data-act="resetCfg">Reset to defaults</button></div></div></div>';
  }
  var HOTKEY_BUTTONS = ['Left', 'Up', 'Right', 'Down', 'Square', 'Triangle', 'Circle', 'Cross', 'Options', 'L1', 'R1', 'L2', 'R2', 'L3', 'R3', 'TouchPad'];
  // Injects into SceShellUI, so this stays off by default and gets its own risk banner.
  function hotkeyCard(c) {
    function btnSel(key, cur) {
      return '<select class="select" data-change="cfgHotkeyBtn" data-k="' + key + '" aria-label="Buttons">'
        + HOTKEY_BUTTONS.map(function (b) { return '<option value="' + b + '"' + (cur === b ? ' selected' : '') + '>' + b + '</option>'; }).join('') + '</select>';
    }
    return '<div class="panel">' + sect('pad', 'Controller shortcut and game menu', 'Adds a \u2605 CheatRunner entry to each game\'s options menu, and a button combo that opens the trainer for the running game.', '', 'warn')
      + '<div class="banner warn">' + ic('warn') + '<div class="txt">Hooks into the system UI. Off by default - on some firmwares this can crash the home screen.</div></div>'
      + '<div class="set-row sw"><div class="set-txt"><b>Enable shortcut</b><span>Hold two buttons together to open the trainer for the running game.</span></div>'
      + '<div class="set-ctl"><button class="switch' + (c.hotkey_enabled ? ' on' : '') + '" data-act="cfgSwitch" data-k="hotkey_enabled" role="switch" aria-checked="' + (c.hotkey_enabled ? 'true' : 'false') + '" aria-label="Enable controller shortcut"></button></div></div>'
      + '<div class="set-row"><div class="set-txt"><b>Buttons</b><span>Held together to trigger the shortcut.</span></div>'
      + '<div class="set-ctl wide">' + btnSel('hotkey_button_a', c.hotkey_button_a) + btnSel('hotkey_button_b', c.hotkey_button_b) + '</div></div>'
      + '<div class="set-row"><div class="set-txt"><b>Hold time (ms)</b><span>How long to hold the combo before it triggers.</span></div>'
      + '<div class="set-ctl"><input class="input" type="number" data-change="cfgNum" data-k="hotkey_hold_ms" min="100" max="5000" value="' + Number(c.hotkey_hold_ms || 800) + '"></div></div>'
      + '</div>';
  }

  /* ---------------------------------------------------------- global polls */
  function paintStatusChip() {
    var chip = $('#statusChip'); if (!chip) return;
    var eng = S.offline ? 'warn' : !S.state ? '' : (S.state.cheats && S.state.cheats.engine) ? 'ok' : 'err';
    var g = S.running ? findGame(S.running) : null;
    chip.className = 'status-chip ' + eng + (S.running ? '' : ' idle');
    $('#stEngine').textContent = S.offline ? t('Reconnecting...') : !S.state ? t('Engine') : eng === 'ok' ? t('Engine on') : t('Engine off');
    $('#stGame').textContent = S.running ? (g ? gname(g) : S.running) : t('No game');
    var d = $('#dotGames'); if (d) d.hidden = !S.running;
  }
  function setOffline(on) {
    if (S.offline === on) return;
    S.offline = on;
    paintStatusChip();
    if (route().page === 'game') paintHero();
    if (!on) refreshState();
  }
  async function refreshStatus() {
    try {
      var s = await get('/api/status', { retries: 0 });
      S.fail = 0; setOffline(false);
      var id = cleanId(s.titleId || s.title_id);
      var running = isTitleId(id) ? id : '';
      S.browserUrl = s.browserUrl || s.browser_url || (location.origin + '/');
      if (running !== S.running) {
        // A game just closed: the console writes its play time then, so reload the list shortly after.
        if (S.running && !running) setTimeout(function () { loadGames(true); }, 4000);
        S.running = running;
        S.games.forEach(function (x) { x.running = gid(x) === running; });
        var p = route().page;
        if (p === 'games') paintGrid(); else if (p === 'game') { paintHero(); paintBody(); }
      }
      paintStatusChip();
    } catch (e) { S.fail = (S.fail || 0) + 1; if (S.fail >= 2) setOffline(true); }
  }
  async function refreshState() {
    try {
      S.state = await get('/api/state', { retries: 0 });
      paintStatusChip();
      Array.prototype.forEach.call(document.querySelectorAll('.ver-text'), function (el) { el.textContent = 'v' + String(S.state.version || '').replace(/^v/, ''); });
      var p = route().page;
      if (p === 'system') paintStatus(); else if (p === 'game') paintHero();
    } catch (e) {}
  }
  async function checkUpdate() {
    var cur = String((S.state || {}).version || '').replace(/^v/, ''), info = null;
    if (!cur) return;
    try { info = JSON.parse(localStorage.getItem('cr_upd') || 'null'); } catch (e) {}
    if (!info || Date.now() - info.at > 6 * 3600000) {
      try {
        var r = await fetch('https://api.github.com/repos/notmaj0r/CheatRunner/releases/latest');
        if (!r.ok) return;
        info = { at: Date.now(), tag: String((await r.json()).tag_name || '') };
        localStorage.setItem('cr_upd', JSON.stringify(info));
      } catch (e) { return; }
    }
    var latest = info.tag.replace(/^v/, ''), a = latest.split('.'), b = cur.split('.'), newer = false;
    for (var i = 0; i < Math.max(a.length, b.length); i++) {
      var x = parseInt(a[i], 10) || 0, y = parseInt(b[i], 10) || 0;
      if (x !== y) { newer = x > y; break; }
    }
    if (!newer) return;
    $('#updLink').hidden = false;
    var seen = ''; try { seen = localStorage.getItem('cr_upd_seen') || ''; } catch (e) {}
    if (seen !== latest) {
      toast(t('Version {v} is available.', { v: latest }), 'info');
      try { localStorage.setItem('cr_upd_seen', latest); } catch (e) {}
    }
  }
  async function refreshLaunch() {
    if (S.launching) return;
    try {
      var ls = await get('/api/launch/status', { retries: 0 });
      var busy = !!ls.busy;
      if (busy !== S.launchBusy) { S.launchBusy = busy; if (route().page === 'games') paintGrid(); }
    } catch (e) {}
  }
  function poller(fn, ms, when) {
    var fails = 0;
    (function tick() {
      var delay = isPS5 ? ms * 2 : ms;
      if (document.hidden) delay = ms * 3;
      else if (!when || when()) {
        Promise.resolve(fn()).then(function () { fails = 0; }, function () { fails++; });
      }
      setTimeout(tick, fails ? Math.min(ms * (1 << Math.min(fails, 4)), 60000) : delay);
    })();
  }

  /* ---------------------------------------------------------------- router */
  function route() {
    var h = location.hash || '#/games';
    var m = /^#trainer=(.+)$/.exec(h);
    if (m) return { page: 'game', id: cleanId(m[1]), tab: 'cheats' };
    var p = h.replace(/^#\/?/, '').split('/');
    if (p[0] === 'game' && p[1]) return { page: 'game', id: cleanId(p[1]), tab: p[2] || 'cheats' };
    if (p[0] === 'sources' || p[0] === 'system' || p[0] === 'settings') return { page: p[0] };
    return { page: 'games' };
  }
  var TITLES = { games: 'Games', game: 'Trainer', sources: 'Sources', system: 'System', settings: 'Settings' };
  var lastKey = '';
  function enter(el) { if (!el || rootCls.contains('lite')) return; el.classList.remove('page-in'); void el.offsetWidth; el.classList.add('page-in'); }
  function render() {
    var r = route(), key = r.page + ':' + (r.id || '');
    Array.prototype.forEach.call(document.querySelectorAll('.rail-item[data-nav]'), function (a) {
      a.classList.toggle('active', a.getAttribute('data-nav') === (r.page === 'game' ? 'games' : r.page));
    });
    if (key !== lastKey) window.scrollTo(0, 0);
    document.title = t('{page} - CheatRunner', { page: t(r.page === 'game' ? 'Trainer' : TITLES[r.page]) });
    if (r.page === 'games') viewGames();
    else if (r.page === 'game') viewGame(r.id, r.tab);
    else if (r.page === 'sources') viewSources();
    else if (r.page === 'system') viewSystem();
    else viewSettings();
    if (key !== lastKey) enter(view); else if (r.page === 'game') enter($('#gBody'));
    lastKey = key;
    if (r.page === 'sources' || r.page === 'system' || r.page === 'settings') padStart($('#view .btn, #view .switch, #view .select'));
  }
  window.addEventListener('hashchange', render);
  (function () {
    var bar = $('.topbar'), busy = false;
    if (!isPS5) window.addEventListener('scroll', function () {
      if (busy) return; busy = true;
      requestAnimationFrame(function () { bar.classList.toggle('scrolled', (window.pageYOffset || 0) > 4); busy = false; });
    }, { passive: true });
  })();

  /* ------------------------------------------------------------------ palette */
  var pal = { on: false, sel: 0, list: [] };
  function palItems(q) {
    var all = [
      { k: 'Page', t: 'Games', s: 'Browse your library', i: 'games', go: '#/games' },
      { k: 'Page', t: 'Sources', s: 'Download community cheats', i: 'sources', go: '#/sources' },
      { k: 'Page', t: 'System', s: 'Status and logs', i: 'system', go: '#/system' },
      { k: 'Page', t: 'Settings', s: 'Theme and cheat engine', i: 'settings', go: '#/settings' }
    ];
    var run = S.running ? findGame(S.running) : null;
    if (run) all.push({ k: 'Action', t: t('Open trainer: {name}', { name: gname(run) }), s: 'Running now', i: 'bolt', go: '#/game/' + S.running });
    all.push({ k: 'Action', t: 'Refresh game list', s: 'Rescan installed games', i: 'refresh', fn: 'refreshGames' });
    all.push({ k: 'Action', t: 'Show QR code', s: 'Open the dashboard on your phone', i: 'qr', fn: 'qr' });
    all.push({ k: 'Action', t: 'Support this project', s: 'Ko-Fi, coffee, crypto, PIX', i: 'heart', fn: 'support' });
    var games = S.games.map(function (g) { return { k: 'Game', t: gname(g), s: gid(g) + ' Â· ' + gplat(g), i: 'play', go: '#/game/' + gid(g) }; });
    if (!q) {
      var rec = S.recent.map(findGame).filter(Boolean).slice(0, 6).map(function (g) { return { k: 'Recent', t: gname(g), s: gid(g), i: 'play', go: '#/game/' + gid(g) }; });
      return all.concat(rec);
    }
    return all.concat(games).filter(function (x) { return fold(x.t + ' ' + x.s).indexOf(q) >= 0; }).slice(0, 40);
  }
  function palRender(q) {
    pal.list = palItems(fold(q || '').trim());
    if (pal.sel >= pal.list.length) pal.sel = 0;
    var box = $('#palList'); if (!box) return;
    box.innerHTML = pal.list.length ? pal.list.map(function (x, i) {
      return '<button class="pal-item' + (i === pal.sel ? ' sel' : '') + '" role="option" aria-selected="' + (i === pal.sel) + '" data-act="palGo" data-i="' + i + '">'
        + ic(x.i, x.i === 'play' ? 'fill sm' : '') + '<span class="pal-txt"><b>' + esc(x.t) + '</b><small>' + esc(x.s) + '</small></span><span class="pal-kind">' + x.k + '</span></button>';
    }).join('') : '<div class="pal-empty">Nothing matches "' + esc(q) + '".</div>';
  }
  function palMove(d) {
    if (!pal.list.length) return;
    pal.sel = (pal.sel + d + pal.list.length) % pal.list.length;
    palRender($('#palQ').value);
    var el = $('#palList .sel'); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest' });
  }
  function palChoose(i) {
    var x = pal.list[i]; if (!x) return;
    closeModal(false);
    if (x.go) go(x.go); else if (x.fn) actions[x.fn]($('#gRefresh') || document.createElement('button'));
  }
  function openPalette() {
    if (!$('#modal').hidden) return;
    pal.sel = 0; pal.on = true;
    openModal('<div class="pal-in">' + ic('search') + '<input id="palQ" data-in="palq" placeholder="Search games, pages and actions" aria-label="Search games, pages and actions" autocomplete="off"><kbd>Esc</kbd></div>'
      + '<div class="pal-list" id="palList" role="listbox"></div>'
      + '<div class="pal-foot"><span>ââ navigate</span><span>Enter open</span></div>', function () { pal.on = false; }, 'palette');
    palRender('');
    $('#palQ').focus();
  }
  function ago(ts) {
    var d = Math.max(0, Math.floor(Date.now() / 1000) - Number(ts || 0));
    if (d < 60) return t('just now');
    if (d < 3600) return t('{n}m ago', { n: Math.floor(d / 60) });
    if (d < 86400) return t('{n}h ago', { n: Math.floor(d / 3600) });
    return t('{n}d ago', { n: Math.floor(d / 86400) });
  }
  function setRail(min) {
    if (min) document.documentElement.setAttribute('data-rail', 'min'); else document.documentElement.removeAttribute('data-rail');
    store('cr_rail', min ? 'min' : 'max');
    var b = $('#railBtn'); if (b) { b.title = min ? 'Expand menu' : 'Collapse menu'; b.setAttribute('aria-label', b.title); }
  }

  /* ---------------------------------------------------------------- language */
  function langName(id) { for (var i = 0; i < CRI.langs.length; i++) if (CRI.langs[i][0] === id) return CRI.langs[i][1]; return id; }
  function langHtml() {
    return '<h2>Language</h2><div class="lang-grid">' + CRI.langs.map(function (l) {
      var on = l[0] === CRI.lang;
      return '<button class="lang-opt' + (on ? ' on' : '') + '" data-act="langSet" data-id="' + l[0] + '" lang="' + l[0] + '"><img class="flag" src="' + CRI.flag(l[0]) + '" alt=""><span>' + esc(l[1]) + '</span>' + (on ? ic('check', 'sm') : '') + '</button>';
    }).join('') + '</div><div class="modal-actions"><button class="btn ghost" data-act="modalNo">Close</button></div>';
  }

  /* -------------------------------------------------------------------- QR */
  function qrSvg(text) {
    var q = qrcode(0, 'M'); q.addData(text); q.make();
    var n = q.getModuleCount(), pad = 3, size = n + pad * 2, d = '';
    for (var r = 0; r < n; r++) for (var c = 0; c < n; c++) if (q.isDark(r, c)) d += 'M' + (c + pad) + ' ' + (r + pad) + 'h1v1h-1z';
    return '<svg width="220" height="220" viewBox="0 0 ' + size + ' ' + size + '" shape-rendering="crispEdges" role="img" aria-label="QR code for the dashboard address">'
      + '<rect width="' + size + '" height="' + size + '" fill="#fff"/><path d="' + d + '" fill="#111"/></svg>';
  }
  function qrHtml() {
    var url = S.browserUrl || location.origin + '/';
    return '<h2>' + ic('qr', 'sm') + ' Open on your phone</h2><p>Scan this with your phone camera. Your phone must be on the same Wi-Fi as the PS5.</p>'
      + '<div class="qr-box">' + qrSvg(url) + '</div><div class="qr-url mono">' + esc(url) + '</div>'
      + '<div class="modal-actions"><button class="btn ghost" data-act="copyUrl">' + ic('copy', 'sm') + 'Copy link</button><button class="btn" data-act="modalNo">Close</button></div>';
  }

  /* --------------------------------------------------------------- support */
  var SUPPORTERS = ['primo', 'lal3x', 'jawad', 'anonymous', 'Maffioh', 'X-F1REBALL-X', 'Br0ken4life', 'Nimrod', 'PCYGOR', 'Lucas Clinio Maraia', 'Vinas', 'Someone', 'Bruno Sanglard', 'AirKingBR', 'Marcus', 'Jeffs', 'cavalcanti27'];
  var NEON = ['#00ff88', '#cc44ff', '#ff9900', '#ffe600', '#00aaff', '#ff4040'];
  var CRYPTO = [
    { label: 'BTC', addr: 'bc1ppfcad9ytyzmmzq3nfatfcag7q38rqcpsvxfqusq0uzv4gh82pa4syeknlk' },
    { label: 'BCH (Bitcoin Cash)', addr: 'qz2h97c2pyexn3fwhveu33tn5xkd3qlfn5796zhtcq' },
    { label: 'USDC (Solana)', addr: 'BNpezXSQWuUR9HWGjzQUS7AeEBCwiY2ZGxArWNwYXKKV' },
    { label: 'USDT (TON)', addr: 'UQA18f4cVS0778fL_nIssdN1GIHRYR7dQ3UnNgjPwgYvIQi_' }
  ];
  function supportHtml(view) {
    if (view === 'crypto') {
      return '<div class="sup-bar"><button class="icon-btn sup-round" data-act="supportBack" aria-label="Back">' + ic('back') + '</button>'
        + '<div class="sup-bar-txt"><h2>' + ic('bitcoin', 'sm') + ' Crypto support</h2><span>Select an address to copy it in full.</span></div>'
        + '<button class="icon-btn sup-round" data-act="modalNo" aria-label="Close">' + ic('x') + '</button></div>'
        + '<div class="sup-list">' + CRYPTO.map(function (c) {
          return '<button class="sup-addr" data-act="copyAddr" data-a="' + esc(c.addr) + '" data-l="' + esc(c.label) + '">'
            + '<div class="sup-addr-top"><b>' + esc(c.label) + '</b><span>Copy</span></div><div class="mono">' + esc(c.addr) + '</div></button>';
        }).join('') + '</div>';
    }
    function opt(cls, icon, title, sub, href) {
      var inner = '<span class="sup-opt-ic">' + ic(icon) + '</span><span class="sup-opt-txt"><b>' + title + '</b>' + (sub ? '<small>' + sub + '</small>' : '') + '</span>' + ic('chev', 'sm');
      return href ? '<a class="sup-opt ' + cls + '" href="' + href + '" target="_blank" rel="noopener noreferrer">' + inner + '</a>'
        : '<button class="sup-opt ' + cls + '" data-act="supportCrypto">' + inner + '</button>';
    }
    return '<div class="sup-hero"><button class="icon-btn sup-round sup-close" data-act="modalNo" aria-label="Close">' + ic('x') + '</button>'
      + '<div class="sup-heart">' + ic('heart') + '</div><h2>Support CheatRunner</h2>'
      + '<p>If CheatRunner was useful to you, consider supporting the project development! ð«¶</p>'
      + '<p class="sup-goal">All contributions will go toward our goal of acquiring a low-firmware PS5 (1.xxâ4.xx) for research, development, and future upgrades.</p></div>'
      + '<div class="sup-grid">'
      + opt('sup-kofi', 'coffee', 'Ko-Fi', '', 'https://ko-fi.com/maj0r')
      + opt('sup-coffee', 'coffee', 'Buy Me a Coffee', '', 'https://buymeacoffee.com/maj0r')
      + opt('sup-crypto', 'bitcoin', 'Crypto', 'Choose a network', '')
      + opt('sup-pix', 'pix', 'PIX', 'Support from Brazil', 'https://livepix.gg/notmaj0r')
      + '</div><div class="sup-thanks"><div class="sup-thanks-h">Supporters</div><div class="sup-names">'
      + SUPPORTERS.map(function (n, i) { return '<span class="sup-name" style="color:' + NEON[i % NEON.length] + ';animation-delay:-' + (i * 0.45).toFixed(2) + 's">' + esc(n) + '</span>'; }).join('')
      + '</div></div>';
  }
  function swapSupport(view) {
    var card = $('#modalCard');
    card.innerHTML = supportHtml(view);
    var h = card.querySelector('h2'); if (h) { h.id = 'mTitle'; card.setAttribute('aria-labelledby', 'mTitle'); }
    var f = card.querySelector('button, a'); if (f) f.focus();
  }

  /* --------------------------------------------------------------- actions */
  var go = function (h) { location.hash = h; };
  var actions = {
    statusGo: function () { go(S.running ? '#/game/' + S.running : '#/system'); },
    goSources: function () { go('#/sources'); },
    goGames: function () { go('#/games'); },
    retryGame: function () { var g = S.g; if (!g) return; g.loaded = false; paintHero(); paintBody(); loadGamePage(g.id); },
    retryState: async function () { var g = S.g; if (!g) return; await loadCheatState(g.id); paintHero(); paintBody(); },
    retryPatches: function () { if (S.g) { S.g.patch = null; loadPatches(); } },
    retrySettings: function () { viewSettings(); },
    repoRetry: function () { var l = S.dlLast || {}; repoStart(l.all, l.patches); },
    logLvl: function (el) {
      logs.min = parseInt(el.getAttribute('data-v'), 10) || 0;
      Array.prototype.forEach.call(document.querySelectorAll('[data-act="logLvl"]'), function (b) { var on = b === el; b.classList.toggle('on', on); b.setAttribute('aria-pressed', on ? 'true' : 'false'); });
      renderLogs();
    },
    logJump: function () { var o = $('#logOut'); if (o) { o.scrollTop = o.scrollHeight; logs.atEnd = true; paintFollow(); } },
    themeSet: function (el) {
      var v = el.getAttribute('data-v'); applyTheme(v); setCfg('theme', v);
      Array.prototype.forEach.call(document.querySelectorAll('[data-act="themeSet"]'), function (b) { var on = b === el; b.classList.toggle('on', on); b.setAttribute('aria-checked', on ? 'true' : 'false'); });
    },
    fxSet: function (el) {
      var v = el.getAttribute('data-v'); store('cr_effects', v); rootCls.toggle('lite', v === 'reduced');
      Array.prototype.forEach.call(document.querySelectorAll('[data-act="fxSet"]'), function (b) { var on = b === el; b.classList.toggle('on', on); b.setAttribute('aria-pressed', on ? 'true' : 'false'); });
    },
    modalYes: function () { closeModal(true); },
    modalNo: function () { closeModal(false); },
    open: function (el) { go('#/game/' + el.getAttribute('data-id')); },
    back: function () { go('#/games'); },
    launch: function (el) { launch(el.getAttribute('data-id')); },
    fav: function (el) {
      var id = el.getAttribute('data-id'); toggleFav(id);
      if (route().page === 'game') paintHero(); else paintGrid();
      var nb = document.querySelector('[data-act="fav"][data-id="' + id + '"]');
      if (nb && S.fav.has(id)) { nb.classList.add('pop'); setTimeout(function () { nb.classList.remove('pop'); }, 450); }
    },
    chip: function (el) { S.filter = el.getAttribute('data-f'); paintGrid(); },
    clearQ: function () { S.q = ''; var i = $('#gq'); if (i) { i.value = ''; i.focus(); } paintGrid(); },
    clearFilters: function () { S.q = ''; S.filter = 'ALL'; var i = $('#gq'); if (i) i.value = ''; paintGrid(); },
    refreshGames: async function (el) {
      var btn = $('#gRefresh') || el;
      if (btn.classList.contains('is-busy')) return;
      btn.classList.add('is-busy'); S.gamesError = false;
      await loadGames(true);
      try { await get('/api/patches/rescan', { retries: 0 }); } catch (e) {}
      btn.classList.remove('is-busy');
    },
    dismissWelcome: function () { store('cr_welcome', '1'); viewGames(); },
    tab: function (el) { go('#/game/' + S.g.id + '/' + el.getAttribute('data-t')); },
    toggle: function (el) { toggleMod(parseInt(el.getAttribute('data-i'), 10), el); },
    disableAll: async function () {
      try { var r = await get('/api/cheats/disable-all?titleId=' + encodeURIComponent(S.g.id), 30000); toast(r && r.disabled ? t('{n} cheat(s) turned off.', { n: r.disabled }) : t('Nothing was on.'), 'success'); }
      catch (e) { toast(e.message || 'Could not disable cheats.', 'error'); }
      await loadCheatState(S.g.id); paintBody();
    },
    saveAuto: async function () {
      var g = S.g, names = [];
      g.doc.mods.forEach(function (m, i) { if (g.rows[i] && STATE_ON[g.rows[i].state] && m.name) names.push(m.name); });
      try { await post('/api/cheats/profile/save?titleId=' + encodeURIComponent(g.id), { mods: names }); g.auto = names; toast(names.length ? t('Autoload saved ({n}). These turn on when the game launches.', { n: names.length }) : t('Autoload saved empty.'), 'success'); paintBody(); }
      catch (e) { toast('Could not save autoload.', 'error'); }
    },
    clearAuto: async function () {
      try { await post('/api/cheats/profile/clear?titleId=' + encodeURIComponent(S.g.id)); S.g.auto = []; toast('Autoload cleared.', 'success'); paintBody(); }
      catch (e) { toast('Could not clear autoload.', 'error'); }
    },
    clearCrash: async function () {
      try { await get('/api/cheats/clear-crash-flags?titleId=' + encodeURIComponent(S.g.id)); }
      catch (e) { toast(e.message || 'Failed.', 'error'); }
      await loadCheatState(S.g.id); paintBody();
    },
    selectFile: async function (el) {
      var wrong = el.getAttribute('data-wrong') === '1';
      if (wrong && !(await confirmBox('Use another version?', 'This file is for a different game version than the one installed.', 'Use anyway', true))) return;
      try {
        await post('/api/cheats/select?titleId=' + encodeURIComponent(S.g.id) + '&path=' + encodeURIComponent(el.getAttribute('data-path')) + (wrong ? '&force=1' : ''));
        S.g.loaded = false; await loadGamePage(S.g.id); paintBody(); toast('Cheat file selected.', 'success');
      } catch (e) { toast(e.message || 'Could not select the file.', 'error'); }
    },
    autoSelect: async function () {
      try { await post('/api/cheats/select/auto?titleId=' + encodeURIComponent(S.g.id)); S.g.loaded = false; await loadGamePage(S.g.id); paintBody(); toast('Auto-select restored.', 'success'); }
      catch (e) { toast(e.message || 'Failed.', 'error'); }
    },
    deleteLocal: async function () {
      var fn = String((S.g.dbg || {}).cheatPath || '').split('/').pop() || t('the cheat file');
      if (!(await confirmBox(t('Delete {file}?', { file: fn }), 'This removes the file from the console. You can download it again from Sources.', 'Delete file', true))) return;
      try { await get('/api/cheats/delete?titleId=' + encodeURIComponent(S.g.id)); toast('Cheat file deleted.', 'warn'); S.g.loaded = false; await loadGames(true); await loadGamePage(S.g.id); paintBody(); }
      catch (e) { toast(e.message || 'Delete failed.', 'error'); }
    },
    pickUpload: function () { var i = document.createElement('input'); i.type = 'file'; i.accept = '.mc4,.shn,.json'; i.onchange = function () { if (i.files[0]) uploadFile(i.files[0]); }; i.click(); },
    patchApply: function (el) { patchCall('apply', parseInt(el.getAttribute('data-i'), 10), false); },
    patchForce: async function (el) {
      var pe = S.g.patch.entries[parseInt(el.getAttribute('data-i'), 10)];
      if (await confirmBox(t('Force apply {name}?', { name: pe ? pe.name : t('patch') }), 'It targets another game version, so it may crash the game. Restore puts the original bytes back.', 'Apply anyway', true)) patchCall('apply', parseInt(el.getAttribute('data-i'), 10), true);
    },
    patchRestore: function (el) { patchCall('restore', parseInt(el.getAttribute('data-i'), 10), false); },
    patchSaveAuto: async function () {
      var ids = (S.g.patch.entries || []).filter(function (e) { return e.applied && e.entryId; }).map(function (e) { return e.entryId; });
      try { await post('/api/patches/profile/save?titleId=' + encodeURIComponent(S.g.id), { entryIds: ids }); S.g.patchAuto = ids; toast('Patch autoload saved.', 'success'); paintPatches(); }
      catch (e) { toast('Could not save autoload.', 'error'); }
    },
    patchClearAuto: async function () {
      try { await post('/api/patches/profile/clear?titleId=' + encodeURIComponent(S.g.id)); S.g.patchAuto = []; toast('Patch autoload cleared.', 'success'); paintPatches(); }
      catch (e) { toast('Could not clear autoload.', 'error'); }
    },
    repoGo: function () { repoStart(false); },
    repoAll: function () { repoStart(true); },
    patchRepoGo: function () { repoStart(false, true); },
    copyUrl: function (el) { copyText(S.browserUrl || location.origin + '/', 'URL copied.', el); },
    copyLogs: async function (el) {
      var text = logs.list.map(logLine).join('\n');
      try { var d = await get('/api/logs/export'); text = (d.logs || []).map(logLine).join('\n') || text; } catch (e) {}
      copyText(text, 'Logs copied.', el);
    },
    shutdown: async function () {
      if (!(await confirmBox('Shut down CheatRunner?', 'Send a new CheatRunner.elf from your PC afterwards.', 'Shut down', true))) return;
      try { await post('/api/dev/shutdown'); } catch (e) {}
      toast('Waiting for CheatRunner to come back...', 'info');
      for (;;) { await sleep(2000); try { await fetch('/api/state?ts=' + Date.now(), { cache: 'no-store' }).then(function (r) { if (r.ok) location.reload(); }); } catch (e) {} }
    },
    cfgSwitch: async function (el) {
      var k = el.getAttribute('data-k'), nv = el.classList.contains('on') ? 0 : 1;
      if (await setCfg(k, nv)) { el.classList.toggle('on', !!nv); el.setAttribute('aria-checked', nv ? 'true' : 'false'); }
    },
    resetCfg: async function () {
      if (!(await confirmBox('Reset settings?', 'All settings go back to their defaults. The web port is kept.', 'Reset', true))) return;
      try { await post('/api/config/reset'); toast('Settings reset.', 'success'); viewSettings(); } catch (e) { toast('Reset failed.', 'error'); }
    },
    reportIssue: async function () {
      try { var d = await get('/api/dev/diag'); await copyText(JSON.stringify(d, null, 2), 'Diagnostics copied. Paste them into the issue.'); } catch (e) { toast('Could not copy diagnostics.', 'warn'); }
      window.open('https://github.com/notmaj0r/CheatRunner/issues', '_blank');
    },
    support: function () { openModal(supportHtml('main'), null, 'support'); },
    gridMore: function () { growGrid(); },
    modMore: function () { if (S.g) { S.g.modShown = (S.g.modShown || MOD_CHUNK) + MOD_CHUNK; paintMods(); } },
    patchMore: function () { if (S.g) { S.g.patchShown = (S.g.patchShown || PATCH_CHUNK) + PATCH_CHUNK; paintPatches(); } },
    langPick: function () { openModal(langHtml(), null, 'lang'); },
    langSet: function (el) { CRI.pick(el.getAttribute('data-id')); },
    qr: function () { if (typeof qrcode === 'undefined') { toast('QR code is not available.', 'warn'); return; } openModal(qrHtml(), null, 'qr'); },
    cmdk: function () { openPalette(); },
    palGo: function (el) { palChoose(parseInt(el.getAttribute('data-i'), 10)); },
    railToggle: function () { setRail(document.documentElement.getAttribute('data-rail') !== 'min'); },
    supportCrypto: function () { swapSupport('crypto'); },
    supportBack: function () { swapSupport('main'); },
    copyAddr: function (el) { copyText(el.getAttribute('data-a'), t('{label} address copied', { label: el.getAttribute('data-l') })); }
  };
  $('#supportBtn').setAttribute('data-act', 'support');
  $('#langFlag').src = CRI.flag(CRI.lang);

  document.addEventListener('click', function (e) {
    if (e.target === $('#modal')) { closeModal(false); return; }
    var t = e.target.closest ? e.target.closest('[data-act]') : null;
    if (!t || t.disabled) return;
    var fn = actions[t.getAttribute('data-act')];
    if (!fn) return;
    if (t.tagName !== 'A') e.preventDefault();
    if (t.getAttribute('data-act') === 'fav' || t.getAttribute('data-act') === 'launch') e.stopPropagation();
    fn(t, e);
  });
  document.addEventListener('input', function (e) {
    var k = e.target.getAttribute && e.target.getAttribute('data-in');
    if (k === 'q') { S.q = e.target.value || ''; clearTimeout(S.qTimer); S.qTimer = setTimeout(paintGrid, isPS5 ? 200 : 60); }
    else if (k === 'modq') { S.g.filter = e.target.value || ''; paintMods(); }
    else if (k === 'palq') { pal.sel = 0; palRender(e.target.value || ''); }
    else if (k === 'logq') { logs.q = e.target.value || ''; renderLogs(); }
  });
  document.addEventListener('change', function (e) {
    var k = e.target.getAttribute && e.target.getAttribute('data-change');
    if (!k) return;
    var v = e.target.value;
    if (k === 'sort') { S.sort = v; paintGrid(); }
    else if (k === 'cfgTheme') { applyTheme(v); setCfg('theme', v); }
    else if (k === 'cfgEffects') { store('cr_effects', v); rootCls.toggle('lite', v === 'reduced'); }
    else if (k === 'cfgLog') setCfg('log_level', v);
    else if (k === 'logLevel') { logs.min = parseInt(v, 10) || 0; renderLogs(); }
    else if (k === 'cfgNum') { var n = parseInt(v, 10); if (!isNaN(n)) setCfg(e.target.getAttribute('data-k'), n); }
    else if (k === 'cfgHotkeyBtn') {
      setCfg(e.target.getAttribute('data-k'), v).then(function (ok) { if (!ok) viewSettings(); });
    }
  });
  document.addEventListener('keydown', function (e) {
    var modalOpen = !$('#modal').hidden;
    if ((e.ctrlKey || e.metaKey) && (e.key === 'k' || e.key === 'K')) { e.preventDefault(); if (pal.on) closeModal(false); else openPalette(); return; }
    if (pal.on && modalOpen) {
      if (e.key === 'ArrowDown') { e.preventDefault(); palMove(1); return; }
      if (e.key === 'ArrowUp') { e.preventDefault(); palMove(-1); return; }
      if (e.key === 'Enter') { e.preventDefault(); palChoose(pal.sel); return; }
    }
    var t = e.target, typing = t && (t.tagName === 'INPUT' || t.tagName === 'SELECT' || t.tagName === 'TEXTAREA');
    if (e.key === 'Escape') {
      if (modalOpen) { closeModal(false); return; }
      if (typing && t.getAttribute('data-in') === 'q' && S.q) { actions.clearQ(); return; }
    }
    if (modalOpen && e.key === 'Tab') {
      var f = $('#modalCard').querySelectorAll('button:not([disabled]), input, a[href]');
      if (!f.length) return;
      var first = f[0], last = f[f.length - 1];
      if (e.shiftKey && t === first) { e.preventDefault(); last.focus(); }
      else if (!e.shiftKey && t === last) { e.preventDefault(); first.focus(); }
      return;
    }
    if (modalOpen && e.key === 'Enter' && t.tagName === 'INPUT') { var ok = $('#modalCard .btn:not(.ghost)'); if (ok) { e.preventDefault(); ok.click(); } return; }
    if (e.key === 'Enter' && t && t.getAttribute && t.getAttribute('role') !== 'switch' && /(^| )(tile|now)( |$)/.test(t.className || '') && t.getAttribute('data-act') === 'open') { actions.open(t); return; }
    if (e.key === '/' && !typing && !modalOpen && $('#gq')) { e.preventDefault(); $('#gq').focus(); }
  });
  document.addEventListener('dragover', function (e) { var z = e.target.closest && e.target.closest('#dropZone'); if (z) { e.preventDefault(); z.classList.add('over'); } });
  document.addEventListener('dragleave', function (e) { var z = e.target.closest && e.target.closest('#dropZone'); if (z) z.classList.remove('over'); });
  document.addEventListener('drop', function (e) {
    var z = e.target.closest && e.target.closest('#dropZone'); if (!z) return;
    e.preventDefault(); z.classList.remove('over');
    if (e.dataTransfer && e.dataTransfer.files[0]) uploadFile(e.dataTransfer.files[0]);
  });

  /* -------------------------------------------------------------- focus keep */
  // The PS5 D-pad only starts moving once something has focus, so give each page a starting control.
  function padStart(el) {
    var ae = document.activeElement;
    if (!isPS5 || !el || (ae && ae !== document.body)) return;
    try { el.focus({ preventScroll: true }); } catch (e) { el.focus(); }
  }
  document.addEventListener('focusin', function (e) {
    var el = e.target;
    if (S.g && el.classList && el.classList.contains('switch') && el.getAttribute('data-act') === 'toggle') S.g.lastSw = el.getAttribute('data-i');
  });
  // Keeps the focused control focused when a list repaints under it.
  function focusSig(el) {
    if (!el || el === document.body) return '';
    var sig = el.tagName.toLowerCase();
    ['data-act', 'data-id', 'data-i', 'data-k', 'data-t', 'data-f'].forEach(function (a) { if (el.hasAttribute(a)) sig += '[' + a + '="' + el.getAttribute(a) + '"]'; });
    return sig;
  }
  function withFocus(box, fn) {
    var ae = document.activeElement, sig = ae && box.contains(ae) ? focusSig(ae) : '';
    fn();
    if (!sig) return;
    var n = null; try { n = box.querySelector(sig); } catch (e) {}
    if (n && n !== document.activeElement) try { n.focus({ preventScroll: true }); } catch (e2) { n.focus(); }
  }

  /* ---------------------------------------------------------------- startup */
  (async function start() {
    var cr = $('.rail-credit'); if (cr) cr.innerHTML = t('Made with {heart} by maj0r', { heart: '<span aria-hidden="true">&#10084;&#65039;</span>' });
    CRI.start();
    $('#cmdKbd').textContent = /Mac|iPhone|iPad/.test(navigator.platform || '') ? '\u2318K' : 'Ctrl K';
    setRail(document.documentElement.getAttribute('data-rail') === 'min');
    render();
    try { var c = await get('/api/config', { retries: 0 }); S.cfg = c; applyTheme(c.theme); } catch (e) {}
    await Promise.all([loadFavs(), refreshState(), refreshStatus()]);
    checkUpdate();
    watchDownload();
    await loadGames(false);
    if (route().page === 'games') viewGames();
    poller(refreshStatus, 7000);
    poller(refreshLaunch, 4000);
    poller(refreshState, 10000);
    poller(refreshLogs, 6000, function () { return route().page === 'system'; });
    poller(async function () {
      var g = S.g; if (!g || route().page !== 'game' || g.tab !== 'cheats' || !g.loaded || Object.keys(g.applying).length) return;
      await loadCheatState(g.id);
      if (S.g === g && route().page === 'game' && g.tab === 'cheats') paintMods();
    }, 6000);
  })();
})();
