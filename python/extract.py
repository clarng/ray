
import re
import nltk
nltk.download('punkt')

ray_usage_dict = {
  ""

}

problem_dict = {
  ""
}

referenced_user_pattern = "/(?<!\w)@\w+/"

pattern_matchers = [referenced_user_pattern]

if __name__ == "__main__":
  post = "@Antoni and I just had a meeting with the creator of Merlion (forecast library developed by SalesForce) following their podcast on Ben Lorica’s channel. I got ticked mostly because SalesForce were looking to go in production with pyspark. I presented this deck and highlighted the benchmarked that Nixtla were running comparing Spark vs Ray (separate thread). (Ray is 4-6 faster than spark + prophet compared to Ray + statsforecast) and also talked through the instacart, doordash stories. Next step is for him to raise the benefits of Ray to his manager and incorporate Ray hopefully into their roadmap. They are particularly interested in RayTune, Batch inference and backtesting. I also asked why there were so many different forecasting libraries and why the need for doing forecasting everywhere. He mentioned a rising need across several areas such as APM (Application performance management), sensors, business forecasting, anomaly detection. More use cases necessitating more data and a trend towards AIOps or operational ML."
  sentences = nltk.sent_tokenize(post)
  for sentence in sentences:
    words = nltk.word_tokenize(sentence)
    print(words)
  
  # token = input
  # m = re.search(referenced_user_pattern, token)
  # print(m)