"""Create persistent canvas objects using an authorized bot or user token.

AGORA_BOT_TOKEN=... python examples/create_objects.py <canvas-id>
Alternatively set AGORA_USER_TOKEN. An existing bot token takes precedence.
"""
import argparse
import os

from agora_canvas import AgoraClient


def main():
    parser = argparse.ArgumentParser(description="기존 캔버스에 도형·텍스트·연결 화살표를 생성합니다.")
    parser.add_argument("canvas_id", type=int, help="접근 권한이 있는 캔버스 ID")
    args = parser.parse_args()
    if args.canvas_id <= 0:
        parser.error("캔버스 ID는 양수여야 합니다.")

    base_url = os.getenv("AGORA_URL", "https://frelog.p-e.kr")
    bot_token = os.getenv("AGORA_BOT_TOKEN")
    user_token = os.getenv("AGORA_USER_TOKEN")
    if not bot_token and not user_token:
        parser.error("AGORA_BOT_TOKEN 또는 AGORA_USER_TOKEN을 환경변수로 설정하세요.")

    client = (AgoraClient.from_bot(bot_token, base_url=base_url) if bot_token
              else AgoraClient(base_url, token=user_token))
    with client:
        with client.canvas(args.canvas_id, password=os.getenv("AGORA_CANVAS_PASSWORD")) as canvas:
            rectangle_id = canvas.create({
                "kind": "shape", "shapeType": "rectangle",
                "x": .15, "y": .25, "width": .2, "height": .14,
                "fill": "#eae4ff", "color": "#7055d6", "strokeWidth": 2,
            })
            text_id = canvas.create({
                "kind": "text", "renderMode": "plain",
                "text": "파이썬에서 추가한 텍스트", "fontSize": 24,
                "x": .55, "y": .25, "width": .3, "height": .2,
            })
            arrow_id = canvas.create({
                "kind": "connector", "from": rectangle_id, "to": text_id,
                "label": "연결", "labelMode": "plain", "fontSize": 14,
                "startHead": "none", "endHead": "triangle", "color": "#7055d6",
            })
            # create() waits for the server's storage commit. Objects remain
            # on the canvas after this script disconnects.
            print("도형 ID:", rectangle_id)
            print("텍스트 ID:", text_id)
            print("화살표 ID:", arrow_id)
            print("저장된 캔버스 revision:", canvas.revision)


if __name__ == "__main__":
    main()
