var game = new Phaser.Game(800, 600, Phaser.AUTO, '', { preload: preload, create: create, update: update });
var score = 0;
var scoreText;
var ws;
var me;
var myPosition = -1;
var others = {};
var tweens = {};

var Position = {
  L_QUEEN: 0,
  L_1: 1,
  L_2: 2,
  R_QUEEN: 3,
  R_1: 4,
  R_2: 5,
};

function preload() {
    game.load.image('sky', 'assets/sky.png');
    game.load.image('ground', 'assets/platform.png');
    game.load.image('star', 'assets/star.png');
    game.load.spritesheet('dude', 'assets/dude.png', 32, 48);
}

function create() {
    game.physics.startSystem(Phaser.Physics.ARCADE);
    game.add.sprite(0, 0, 'sky');
    
    platforms = game.add.group();
    platforms.enableBody = true;
    
    ground = platforms.create(0, game.world.height - 64, 'ground');
    ground.scale.setTo(2, 2);
    ground.body.immovable = true;
    
    ledge = platforms.create(400, 400, 'ground');
    ledge.body.immovable = true;
    ledge = platforms.create(-150, 250, 'ground');
    ledge.body.immovable = true;
    
    cursors = game.input.keyboard.createCursorKeys();
    
    stars = game.add.group();
    stars.enableBody = true;
    for (var i = 0; i < 12; i++) {
        star = stars.create(i * 70, 0, 'star');
        star.body.gravity.y = 60;
        star.body.bounce.y = 0.1 + Math.random() * 0.2;
    }
    
    scoreText = game.add.text(16, 16, 'score: 0', { fontSize: '32px', fill: '#000' });
    
    ws = new WebSocket("ws://192.168.10.105:9002");
    //ws = new WebSocket("ws://antgame.duckdns.org:9002");
    ws.binaryType = 'arraybuffer';
    endian = isLittleEndian();
    ws.onmessage = onWhoAmIMessage;
    
    buffer = new ArrayBuffer(6);
    view = new DataView(buffer);
    view.setUint8(0, 187); // serves as a magic number for data validation
}

var a = false;
var x = 0,y = 0;
var only_even = 0;
function update() {
    if (!(me instanceof Phaser.Sprite)) return;
    
    hitPlatform = game.physics.arcade.collide(me, platforms);
    
    me.body.velocity.x = 0;
    var key;
    if (cursors.left.isDown) {
        me.body.velocity.x = -150;
        me.animations.play('left');
        a = true;
        key = "L";
    } else if (cursors.right.isDown) {
        me.body.velocity.x = 150;
        me.animations.play('right');
        a = true;
        key = "R";
    } else {
        me.body.x = Math.round(me.body.x);
        me.animations.stop();
        me.frame = 4;
        key = "N";
        if (a) {
            a = false;
        }
    }
    
    if (cursors.up.isDown && me.body.touching.down && hitPlatform) {
        me.body.velocity.y = -350;
    }
    
    game.physics.arcade.collide(stars, platforms);
    game.physics.arcade.overlap(me, stars, collectStar, null, this);
    
    if (only_even++ >= 1) {
        only_even = 0;
        if (x != Math.round(me.body.x) || y != Math.round(me.body.y)) {
            x = Math.round(me.body.x);
            y = Math.round(me.body.y);
            //console.debug(x+","+y);
            if (ws.readyState === 1) {
                view.setUint16(1, x, endian);
                view.setUint16(3, y, endian);
                view.setUint8(5, key.charCodeAt(0));
                ws.send(buffer);
            }
        }
    }
}

function collectStar (player, star) {
    star.kill();
    score += 10;
    scoreText.text = 'Score: ' + score;
}

function isLittleEndian() {
  var a = new ArrayBuffer(4);
  var b = new Uint8Array(a);
  var c = new Uint32Array(a);
  b[0] = 0xa1;
  b[1] = 0xb2;
  b[2] = 0xc3;
  b[3] = 0xd4;
  if (c[0] === 0xd4c3b2a1) {
    return true;
  }
  if (c[0] === 0xa1b2c3d4) {
    return false;
  } else {
    throw new Error('Unrecognized endianness');
  }
}

function createMe(pos, x, y) {
    me = game.add.sprite(x, game.world.height - y, 'dude');
    game.physics.arcade.enable(me);
    me.body.gravity.y = 300;
    me.body.collideWorldBounds = true;
    me.body.bounce.y = 0.2;
    me.animations.add('left', [0, 1, 2, 3], 10, true);
    me.animations.add('right', [5, 6, 7, 8], 10, true);
    
    myPosition = pos;
}

function createOther(pos, x, y, key) {
    others[pos] = game.add.sprite(x, y, 'dude');
    game.physics.arcade.enable(others[pos]);
    others[pos].body.gravity.y = 0;
    others[pos].animations.add('left', [0, 1, 2, 3], 10, true);
    others[pos].animations.add('right', [5, 6, 7, 8], 10, true);
    animate(others[pos], key);
}

function animate(player, key) {
    switch (key) {
    case "L":
        player.animations.play('left');
        break;
    case "R":
        player.animations.play('right');
        break;
    case "N":
        player.animations.stop();
        player.frame = 4;
        break;
    }
}

function onWhoAmIMessage(evt) {
    var found = false;
    
    if (typeof(evt.data) != 'string') {
        return;
    }
    
    switch(parseInt(evt.data)) {
    case (Position.L_QUEEN):
        createMe(Position.L_QUEEN, 32, 150);
        found = true;
        break;
    case (Position.L_1):
        createMe(Position.L_1, 100, 250);
        found = true;
        break;
    case (Position.L_2):
        createMe(Position.L_2, 150, 350);
        found = true;
        break;
    case (Position.R_QUEEN):
        createMe(Position.R_QUEEN, 200, 450);
        found = true;
        break;
    case (Position.R_1):
        createMe(Position.R_1, 250, 550);
        found = true;
        break;
    case (Position.R_2):
        createMe(Position.R_2, 300, 150);
        found = true;
        break;
    }
    
    if (found) {
        ws.onmessage = onWSMessage;
    }
}

function onWSMessage(evt) {
    var data = evt.data;
    var dv = new DataView(data);
    var statusSize = 6; // 6 bytes
    var statusCount = dv.byteLength / statusSize;
    var max = statusCount*statusSize;
    
    for (var i = 0; i < max; i+=statusSize) {
        var pos = dv.getUint8(i+0);
        if (pos === myPosition) continue;
        
        var x = dv.getUint16(i+1, endian);
        var y = dv.getUint16(i+3, endian);
        var key = String.fromCharCode(dv.getUint8(i+5));
        //console.debug(player+","+x+","+y+","+key);
        
        if (typeof others[pos] === 'undefined') {
            createOther(pos, x, y, key);
        } else {
            if (typeof tweens[pos] != 'undefined') {
                game.tweens.remove(tweens[pos]);
            }
            tweens[pos] = game.add.tween(others[pos]);
            tweens[pos].to({ x: x, y: y }, 50, "Linear", true);
            animate(others[pos], key);
        }
    }
    //console.debug("----------------------");
}
