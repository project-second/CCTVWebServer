# 프로젝제트 설명
- Localhost 에 있는 rtsp 를 ffmpeg 으로 받아서 websocket 으로 client 한테 reply 하는 서버

## 관리자
- 최초의 관리자 계정은 ID : admin, Password : admin 으로 되어있다.
- 모든 User 는 Id, Password, Username 총 3가지는 무조건 가지고 있어야하며 Password 와 Username 은 변동이 가능해야한다.
- 관리자 계정은 모든 권한을 쥐고 있으며 관리자의 승인이있어야하지만 User 의 가입이 가능하다. 
- 또한 관리자 계정은 일방적으로 유저를 추방시키는게 가능하다.


## DB
- DB 에는 2가지 table 이 필요한데 하나는 외부에서 Client 가 로그인을 하는 Table 이 필요하고 또한 localhost 에 있는 rtsp 를 받아오기 위한 로그인 정보가 필요하다.
- 모든 로그인 정보는 삭제의 대상이 아닌 보관의 대상이며 추방시에는 접근에 false 조건을 주며 회원가입시 false 된 ID 와 Password 를 가진 유저는 가입이 불가능해야한다.
- DB 의 모든 정보는 암호화되어야한다.

## Network Protocol
- 기본적으로 Http 통신을 사용하며 이는 boost::beast, boost::asio 로 구현이 된다.
- WebSocket 을 사용하며 프로젝트 설명에 나와있듯이 Localhost 에 있는 rtsp 를 ffmpeg 으로 받아서 websocket 으로 client 한테 reply 하는 서버이다.
- AJAX 통신이 필요할 때가 있을 수 있다.
## 디자인
- 로고디자인은 없으며 오로지 폰트로만 정한다 글자는 ***VE*** 를 사용하며 폰트는 추후 추가된다.
- 전체적으로 블랙 앤 화이트 의 심플한 디자인이어야하며 Darkmode, Whitemode 간의 전환이 가능해야 한다.
- Header 에는 기능을 Page 는 Sidebar 를 사용한다.
- Page 는 MyPage, AdminPage, SettingsPage, LoginPage, RegistePage, HomePage 그리고 reply 를 하는 camPage 가 있다.
- 둥근 디자인을 주로 사용하며 radius 는 통일되어야한다. 그리고 Hover 시 Click 시 Effect 및 색깔도 통일되어야한다.
- Media Query 즉 화면 크기에 따른 반응형 웹이 지원되어야한다. 
