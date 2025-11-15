
from openai import OpenAI

# 클라이언트 생성 (환경변수의 OPENAI_API_KEY 사용)
client = OpenAI()

def ask_chatgpt(user_message: str) -> str:
    """
    user_message를 ChatGPT(예: gpt-5.1-mini)에게 보내고
    모델의 답변 문자열을 그대로 리턴하는 함수
    """
    response = client.chat.completions.create(
        model="gpt-5.1-mini",  # 원하면 gpt-5.1, o3-mini 등으로 변경 가능
        messages=[
            {"role": "system", "content": "You are a helpful assistant."},
            {"role": "user", "content": user_message},
        ],
    )

    # 가장 첫 번째 후보의 메시지 내용
    return response.choices[0].message.content

if __name__ == "__main__":
    question = "Hello Open AI."
    answer = ask_chatgpt(question)
    print("User:", question)
    print("Assistant:", answer)
