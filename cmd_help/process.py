# convert_to_c_string.py
def to_c_string(path):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    # escape backslashes and quotes
    text = text.replace("\\", "\\\\").replace("\"", "\\\"")
    # replace newlines with \n
    text = text.replace("\n", "\\n\"\n\"")
    return "\"" + text + "\""

if __name__ == "__main__":
    print(to_c_string("cmd_help.txt"))
